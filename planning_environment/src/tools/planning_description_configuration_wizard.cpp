/*
 * Copyright (c) 2011, Willow Garage, Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <ORGANIZATION> nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

// Author: E. Gil Jones

#include <termios.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <math.h>

#include <ros/ros.h>
#include <ros/package.h>

#include <planning_environment/models/collision_models.h>
#include <planning_environment/models/model_utils.h>
#include <tf/transform_broadcaster.h>
#include <collision_space/environmentODE.h>
#include <rosgraph_msgs/Clock.h>
#include <planning_environment/util/collision_operations_generator.h>

#include <ncurses.h>
#include <tinyxml/tinyxml.h>

//in 100 hz ticks
static const unsigned int CONTROL_SPEED = 10;
static const std::string VIS_TOPIC_NAME = "planning_description_configuration_wizard";
static const double DEFAULT_ACCELERATION = 1.0;

WINDOW* left_win;
WINDOW* right_win;

class PlanningDescriptionConfigurationWizard {
public:  

  PlanningDescriptionConfigurationWizard(const std::string& urdf_package, const std::string& urdf_path) :
    inited_(false), world_joint_config_("world_joint"), urdf_package_(urdf_package), urdf_path_(urdf_path)
  {
    std::string full_urdf_path = ros::package::getPath(urdf_package_)+urdf_path_;

    ROS_INFO_STREAM("full path name is " << full_urdf_path);

    urdf_ = boost::shared_ptr<urdf::Model>(new urdf::Model());
    bool urdf_ok = urdf_->initFile(full_urdf_path);
    
    if(!urdf_ok) {
      ROS_WARN_STREAM("Urdf file " << full_urdf_path << " not ok");
      return;
    }

    //making directories
    dir_name_ = getRobotName()+"_arm_navigation"; 

    std::string del_com = "rm -rf " + dir_name_;
    int ok = system(del_com.c_str());

    std::string mdir = "roscreate-pkg "+dir_name_;
    mdir += " planning_environment arm_kinematics_constraint_aware ompl_ros_interface ";
    mdir += "trajectory_filter_server constraint_aware_spline_smoother move_arm";
    ok = system(mdir.c_str());
    
    mdir ="mkdir -p "+dir_name_+"/config";
    ok = system(mdir.c_str());
    if(ok != 0) {
      ROS_WARN_STREAM("Making subdirectory not ok");
      return;
    }

    mdir = "mkdir -p "+dir_name_+"/launch";
    ok = system(mdir.c_str());
    if(ok != 0) {
      ROS_WARN_STREAM("Making subdirectory not ok");
      return;
    }

    yaml_outfile_name_ = getRobotName()+"_planning_description.yaml";
    full_yaml_outfile_name_ = dir_name_+"/config/"+yaml_outfile_name_;
    launch_outfile_name_ = getRobotName()+"_planning_environment.launch";
    full_launch_outfile_name_ = dir_name_+"/launch/"+launch_outfile_name_;

    //pushing to the param server
    std::string com = "rosparam set robot_description -t "+full_urdf_path;

    ok = system(com.c_str());
    
    if(ok != 0) {
      ROS_WARN_STREAM("Setting parameter system call not ok");
      return;
    }

    cm_ = NULL;
    ops_gen_ = NULL;

    if(!setupWithWorldFixedFrame("", "Floating")) {
      return;
    }

    vis_marker_publisher_ = nh_.advertise<visualization_msgs::Marker>(VIS_TOPIC_NAME, 128);
    vis_marker_array_publisher_ = nh_.advertise<visualization_msgs::MarkerArray>(VIS_TOPIC_NAME+"_array", 128);

    inited_ = true;
  }

  ~PlanningDescriptionConfigurationWizard() {
    if(cm_ != NULL) { 
      delete cm_;
    }
    if(ops_gen_ != NULL) {
      delete ops_gen_;
    }
  }

  void deleteKinematicStates() {
    if(robot_state_ != NULL) {
      delete robot_state_;
      robot_state_ = NULL;
    }
  }

  bool setupWithWorldFixedFrame(const std::string& world_fixed_frame, const std::string& joint_type) 
  {
    lock_.lock();

    std::vector<planning_models::KinematicModel::GroupConfig> gcs;
    std::vector<planning_models::KinematicModel::MultiDofConfig> multi_dof_configs;
    const urdf::Link *root = urdf_->getRoot().get();
 
    std::string ff;

    if(world_fixed_frame.empty()) {
      ff = root->name;
    } else {
      ff = world_fixed_frame;
    }
    
    ROS_INFO_STREAM("Setting world frame to " << ff);

    //now this should work with an n=on-identity transform
    world_joint_config_.type = joint_type;
    world_joint_config_.parent_frame_id = ff;
    world_joint_config_.child_frame_id = root->name;
    multi_dof_configs.push_back(world_joint_config_);

    deleteKinematicStates();

    if(cm_ != NULL) { 
      delete cm_;
    }
    if(ops_gen_ != NULL) {
      delete ops_gen_;
    }
    kmodel_ = new planning_models::KinematicModel(*urdf_, gcs, multi_dof_configs);

    if(kmodel_->getRoot() == NULL) {
      ROS_INFO_STREAM("Kinematic root is NULL");
      lock_.unlock();
      return false;
    }

    robot_state_ = new planning_models::KinematicState(kmodel_);
    robot_state_->setKinematicStateToDefault();
    
    lock_.unlock();

    return true;
  }

  void emitWorldJointYAML() {

    emitter_ << YAML::Key << "multi_dof_joints";
    emitter_ << YAML::Value << YAML::BeginSeq; 
    emitter_ << YAML::BeginMap; 
    emitter_ << YAML::Key << "name" << YAML::Value << world_joint_config_.name;
    emitter_ << YAML::Key << "type" << YAML::Value << world_joint_config_.type;
    emitter_ << YAML::Key << "parent_frame_id" << YAML::Value << world_joint_config_.parent_frame_id;
    emitter_ << YAML::Key << "child_frame_id" << YAML::Value << world_joint_config_.child_frame_id;
    emitter_ << YAML::EndMap;
    emitter_ << YAML::EndSeq;
  }

  void setupGroups() {

    while(1) {
      clear();
      std::vector<std::string> group_names;
      kmodel_->getModelGroupNames(group_names);
      printw("Current groups:\n");
      for(unsigned int i = 0; i < group_names.size(); i++) {
        printw("%d) %s\n", i, group_names[i].c_str());
      }
      printw("Enter 0 to accept current group set\n");
      printw("Enter an 'x' followed by the group number to delete a current group.\n");
      printw("Enter 1 to add a group based on kinematic chain\n");
      printw("Enter 2 to add a group based on a joint collection\n");
      printw("Enter 3 to add a group based on a subgroup collection\n");
      refresh();
      char str[80];
      getstr(str); 
      if(str[0] == 'x') {
        unsigned int entry;
        std::stringstream ss(&str[0]);
        ss >> entry;
        lock_.lock();
        deleteKinematicStates();
        kmodel_->removeModelGroup(group_names[entry]);
        robot_state_ = new planning_models::KinematicState(kmodel_);
        robot_state_->setKinematicStateToDefault();
        lock_.unlock();
      } else {
        unsigned int entry;
        std::stringstream ss(str);
        ss >> entry;
        if(entry == 0) break;
        printw("Enter name for new group: ");
        getstr(str);
        std::stringstream ss2(str);
        std::string new_group_name;
        ss2 >> new_group_name;
        if(entry == 0) {
          break;
        } else if(entry == 1) {
          setupGroupKinematicChain(new_group_name);
        } else if(entry == 2) {
          setupGroupJointCollection(new_group_name);
        } // else {
        //   setupGroupSubgroupCollection(new_group_name);
        // }
      }
    }
  }

  void setupGroupKinematicChain(const std::string& new_group_name) {
    const std::vector<planning_models::KinematicModel::LinkModel*>& lmv = kmodel_->getLinkModels();
    bool has_base = false;
    unsigned int base_num = 0;
    bool has_tip = false;
    unsigned int tip_num = 0;
    bool group_ok = false;
    std::string last_status;
    while(1) {
      clear();
      refresh();
      for(unsigned int i = 0; i < lmv.size(); i++) {
        printw("%-3d ", i);
        if(has_base && i == base_num) {
          printw("(B) ");
        } else if(has_tip && i == tip_num) {
          printw("(T) ");
        } else {
          printw("( )");
        }
        printw("%s\n", lmv[i]->getName().c_str());
      }
      printw("New group name: %s\n", new_group_name.c_str());
      printw("Enter 'b' followed by a link number to set the base link for the group.\n");
      printw("Enter 't' followed by a link number to set the base link for the group.\n");
      printw("Enter 'q' to exit\n");
      if(has_tip && has_base) {
        printw("Enter 'x' to validate/visualize the group\n");
      } 
      if(group_ok) {
        printw("Visualization shows group links in red and updated links in green\n");
        printw("Enter 'a' to accept group\n");
      }
      if(!last_status.empty()) {
        printw("Last status msg: %s\n", last_status.c_str());
      }
      refresh();
      char str[80];
      getstr(str); 
      if(str[0] == 'q') {
        break;
      } else if(str[0] == 'b' || str[0] == 't') {
        std::stringstream ss(&str[1]);
        unsigned int entry;
        ss >> entry;
        if(str[0] == 'b') {
          base_num = entry;
          has_base = true;
        } else {
          tip_num = entry;
          has_tip = true;
        }
      } else if(has_tip && has_base) {
        if(str[0] == 'a') {
          if(!group_ok) {
            last_status = "Must validate group before accepting";
            continue;
          } else {
            current_show_group_ = "";
            break;
          }
        }
        lock_.lock();
        deleteKinematicStates();
        if(kmodel_->hasModelGroup(new_group_name)) {
          kmodel_->removeModelGroup(new_group_name);
        }
        planning_models::KinematicModel::GroupConfig gc(new_group_name,
                                                        lmv[base_num]->getName(),
                                                        lmv[tip_num]->getName());
        group_ok = kmodel_->addModelGroup(gc);
        robot_state_ = new planning_models::KinematicState(kmodel_);
        robot_state_->setKinematicStateToDefault();
        if(group_ok) {
          current_show_group_ = new_group_name;
          last_status = "Group " + current_show_group_ + " ok";
        } else {
          current_show_group_ = "";
          last_status = "Group not ok";
        }
        lock_.unlock();
      }
    }
  }

  void setupGroupJointCollection(const std::string& new_group_name) {
    const std::vector<planning_models::KinematicModel::JointModel*>& jmv = kmodel_->getJointModels();
    std::vector<bool> is_included(jmv.size(), false);
    while(1) {
      clear();
      for(unsigned int i = 0; i < jmv.size(); i++) {
        printw("%-3d ", i);
        if(is_included[i]) {
          printw("(X) ");
        } else {
          printw("( )");
        }
        printw("%s\n", jmv[i]->getName().c_str());
      }
      printw("New group name: %s\n", new_group_name.c_str());
      printw("Enter a joint number or two numbers seperated by a ':' to toggle inclusion\n");
      printw("Enter an 'a' followed by a joint number to toggle that joint and all downstream joints\n");
      printw("Enter 'r' to reset all entries\n");
      printw("Enter 'v' to visualize all member and updated links of the current selection (shown in green)\n");
      printw("Enter 'x' to accept this joint collection\n");
      refresh();
      char str[80];
      getstr(str); 
      if(str[0] == 'x' || str[0] == 'v') {
        lock_.lock();
        std::vector<std::string> joints;
        for(unsigned int i = 0; i < is_included.size(); i++) {
          if(is_included[i]) {
            joints.push_back(jmv[i]->getName());
          }
        }
        deleteKinematicStates();
        if(kmodel_->hasModelGroup(new_group_name)) {
          kmodel_->removeModelGroup(new_group_name);
        }
        std::vector<std::string> emp;
        planning_models::KinematicModel::GroupConfig gc(new_group_name,
                                                        joints,
                                                        emp);
        bool group_ok = kmodel_->addModelGroup(gc);
        robot_state_ = new planning_models::KinematicState(kmodel_);
        robot_state_->setKinematicStateToDefault();
        if(!group_ok) {
          ROS_ERROR_STREAM("Joint collection group really should be ok");
          current_show_group_ = "";
        } else {
          if(str[0] == 'v') {
            current_show_group_ = new_group_name;
          }
        }
        lock_.unlock();
        if(str[0] == 'x') {
          break;
        }
      } else if(str[0] == 'r') {
        for(unsigned int i = 0; i < is_included.size(); i++) {
          is_included[i] = false;
        }
      } else if(str[0] == 'a') {
        std::stringstream ss(&str[1]);
        unsigned int entry;
        ss >> entry;
        std::vector<std::string> joints = kmodel_->getChildJointModelNames(jmv[entry]);
        for(unsigned int i = 0; i < joints.size(); i++) {
          for(unsigned int j = 0; j < jmv.size(); j++) {
            if(joints[i] == jmv[j]->getName()) {
              is_included[j] = !is_included[j];
              break;
            }
          }
        }
      } else {
        unsigned int entry;
        std::stringstream ss(str);
        ss >> entry;
        char c;
        ss >> c;
        if(c != ':') {
          is_included[entry] = !is_included[entry];
        } else {
          unsigned int entry2;
          ss >> entry2;
          for(unsigned int q = entry; q <= entry2; q++) {
            is_included[q] = !is_included[q];
          }
        }
      }
    }
    lock_.lock();
    current_show_group_ = "";
    lock_.unlock();
  }


  void emitGroupYAML() {
    emitter_ << YAML::Key << "groups";
    emitter_ << YAML::Value << YAML::BeginSeq; 

    const std::map<std::string, planning_models::KinematicModel::GroupConfig>& group_config_map = kmodel_->getJointModelGroupConfigMap();

    for(std::map<std::string, planning_models::KinematicModel::GroupConfig>::const_iterator it = group_config_map.begin();
        it != group_config_map.end();
        it++) {
      emitter_ << YAML::BeginMap; 
      emitter_ << YAML::Key << "name" << YAML::Value << it->first;
      if(!it->second.base_link_.empty()) {
        emitter_ << YAML::Key << "base_link" << YAML::Value << it->second.base_link_;
        emitter_ << YAML::Key << "tip_link" << YAML::Value << it->second.tip_link_;
      } else {
        if(!it->second.subgroups_.empty()) {
          emitter_ << YAML::Key << "subgroups";
          emitter_ << YAML::Value << YAML::BeginSeq;
          for(unsigned int i = 0; i < it->second.subgroups_.size(); i++) {
            emitter_ << it->second.subgroups_[i];
          } 
          emitter_ << YAML::EndSeq;
        }
        if(!it->second.joints_.empty()) {
          emitter_ << YAML::Key << "joints";
          emitter_ << YAML::Value << YAML::BeginSeq;
          for(unsigned int i = 0; i < it->second.joints_.size(); i++) {
            emitter_ << it->second.joints_[i];
          } 
          emitter_ << YAML::EndSeq;
        }
      }
      emitter_ << YAML::EndMap;
    }
    emitter_ << YAML::EndSeq;
  }

  // void setupGroupSubgroupCollection(const std::string& new_group_name) {
  //   while(1) {
  //     clear();
  //     std::vector<std::string> group_names;
  //     kmodel_->getModelGroupNames(group_names);
  //     std::vector<bool> is_included(group_names.size(), false);
  //     for(unsigned int i = 0; i < group_names.size(); i++) {
  //       printw("%d) ", i);
  //       if(is_included[i]) {
  //         printw("(X)");
  //       } else {
  //         printw("( )");
  //       }
  //       printw(" %s\n", group_names[i].c_str());
  //     }
  //     printw("Enter a subgroup number to toggle inclusion\n");
  //     printw("Enter 'v' to visualize current subgroup\n");
  //     printw("Enter 'x' to accept current subgroup\n");
  //     refresh();
  //     char str[80];
  //     getstr(str); 
  //     unsigned int entry;
  //     std::stringstream ss(str);
  //     ss >> entry;
  //     if(entry == 0) {
        
  //     }
  //   }
  // }

  void outputJointLimitsYAML() {
    std::map<std::string, bool> already;
    boost::shared_ptr<urdf::Model> robot_model = urdf_;
    YAML::Emitter emitter;
    emitter << YAML::BeginMap;
    emitter << YAML::Key << "joint_limits";
    emitter << YAML::Value << YAML::BeginMap;
    const std::map<std::string, planning_models::KinematicModel::JointModelGroup*>& group_map = kmodel_->getJointModelGroupMap();
    for(std::map<std::string, planning_models::KinematicModel::JointModelGroup*>::const_iterator it = group_map.begin();
        it != group_map.end();
        it++) {
      const std::vector<const planning_models::KinematicModel::JointModel*>& jmv = it->second->getJointModels();
      
      for(unsigned int i = 0; i < jmv.size(); i++) {
        boost::shared_ptr<const urdf::Joint> urdf_joint = robot_model->getJoint(jmv[i]->getName());
        double max_vel = 0.0;
        if(urdf_joint) {
          max_vel = urdf_joint->limits->velocity;
        }
        const std::map<std::string, std::pair<double, double> >& bounds_map = jmv[i]->getAllVariableBounds();
        for(std::map<std::string, std::pair<double, double> >::const_iterator it2 = bounds_map.begin();
            it2 != bounds_map.end(); 
            it2++) {
          if(already.find(it2->first) == already.end()) {
            already[it2->first] = true;
            emitter << YAML::Key << it2->first;
            emitter << YAML::Value << YAML::BeginMap;
            emitter << YAML::Key << "has_position_limits";
            const planning_models::KinematicModel::RevoluteJointModel* rev 
              = dynamic_cast<const planning_models::KinematicModel::RevoluteJointModel*>(jmv[i]);
            bool has_limits = (rev == NULL || !rev->continuous_);
            emitter << YAML::Value << has_limits;
            emitter << YAML::Key << "has_velocity_limits" << YAML::Value << "true";
            emitter << YAML::Key << "max_velocity" << YAML::Value << max_vel;
            emitter << YAML::Key << "has_acceleration_limits" << YAML::Value << "true";
            emitter << YAML::Key << "max_acceleration" << YAML::Value << DEFAULT_ACCELERATION;
            emitter << YAML::Key << "angle_wraparound" << YAML::Value << !has_limits;
            emitter << YAML::EndMap;
          }
        }
      }
    }
    emitter << YAML::EndMap;
    emitter << YAML::EndMap;
    std::ofstream outf((dir_name_+"/config/joint_limits.yaml").c_str(), std::ios_base::trunc);
    
    outf << emitter.c_str();
  }

  void setJointsForCollisionSampling() {
    ode_collision_model_ = new collision_space::EnvironmentModelODE();

    const std::vector<planning_models::KinematicModel::LinkModel*>& coll_links = kmodel_->getLinkModelsWithCollisionGeometry();
    
    std::vector<std::string> coll_names;
    for(unsigned int i = 0; i < coll_links.size(); i++) {
      coll_names.push_back(coll_links[i]->getName());
    }
    collision_space::EnvironmentModel::AllowedCollisionMatrix default_collision_matrix(coll_names,false);
    std::map<std::string, double> default_link_padding_map;
    ode_collision_model_->setRobotModel(kmodel_, default_collision_matrix, 
                                        default_link_padding_map, 0.0, 1.0);


    cm_ = new planning_environment::CollisionModels(urdf_,
                                                    kmodel_, 
                                                    ode_collision_model_);
    ops_gen_ = new planning_environment::CollisionOperationsGenerator(cm_);

    const std::vector<planning_models::KinematicModel::JointModel*>& jmv = cm_->getKinematicModel()->getJointModels();
    std::vector<bool> consider_dof;
    //assuming that 0th is world joint, which we don't want to include
    for(unsigned int i = 1; i < jmv.size(); i++) {
      const std::map<std::string, std::pair<double, double> >& joint_bounds = jmv[i]->getAllVariableBounds();
      for(std::map<std::string, std::pair<double, double> >::const_iterator it = joint_bounds.begin();
          it != joint_bounds.end();
          it++) {
        consider_dof.push_back(true);
      }
    }

    while(1) {
      clear();
      refresh();
      int ind = 1;
      for(unsigned int i = 1; i < jmv.size(); i++) {
        const std::map<std::string, std::pair<double, double> >& joint_bounds = jmv[i]->getAllVariableBounds();
        for(std::map<std::string, std::pair<double, double> >::const_iterator it = joint_bounds.begin();
            it != joint_bounds.end();
            it++) {
          printw("%d) (%c) Dof name: %s  Lower bound: %g   Upper bound: %g\n", ind, (consider_dof[ind-1] ? 'X' : ' '), 
                 it->first.c_str(), it->second.first, it->second.second);
          ind++;
        }
      }
      printw("Enter a number to toggle DOF for collision sampling purposes, two numbers seperated by a ':' to toggle a range(inclusive), or 0 to accept\n");
      refresh();
      char str[80];
      getstr(str); 
      unsigned int entry;
      std::stringstream ss(str);
      ss >> entry;
      if(entry == 0) break;
      char c;
      ss >> c;
      if(c != ':') {
        consider_dof[entry-1] = !consider_dof[entry-1];
      } else {
        unsigned int entry2;
        ss >> entry2;
        for(unsigned int q = entry-1; q < entry2; q++) {
          consider_dof[q] = !consider_dof[q];
        }
      }
    }
    int xind = 0;
    std::map<std::string, bool> cdof_map;
    for(unsigned int i = 1; i < jmv.size(); i++) {
      const std::map<std::string, std::pair<double, double> >& joint_bounds = jmv[i]->getAllVariableBounds();
      for(std::map<std::string, std::pair<double, double> >::const_iterator it = joint_bounds.begin();
          it != joint_bounds.end();
          it++) {
        cdof_map[it->first] = consider_dof[xind++];
      }
    }
    clear();
    refresh();
    ops_gen_->generateSamplingStructures(cdof_map);  
}
  

  visualization_msgs::Marker transformEnvironmentModelContactInfoMarker(const collision_space::EnvironmentModel::Contact& c) {
    std::string ns_name;
    ns_name = c.body_name_1;
    ns_name +="+";
    ns_name += c.body_name_2;
    visualization_msgs::Marker mk;
    mk.header.stamp = ros::Time::now();
    mk.header.frame_id = cm_->getWorldFrameId();
    mk.ns = ns_name;
    mk.id = 0;
    mk.type = visualization_msgs::Marker::SPHERE;
    mk.action = visualization_msgs::Marker::ADD;
    mk.pose.position.x = c.pos.x();
    mk.pose.position.y = c.pos.y();
    mk.pose.position.z = c.pos.z();
    mk.pose.orientation.w = 1.0;
    
    mk.scale.x = mk.scale.y = mk.scale.z = 0.1;
    return mk;
  }

  void considerAlwaysAndDefaultInCollisionMarkers() {
    std::vector<planning_environment::CollisionOperationsGenerator::StringPair> always_in_collision;
    std::vector<planning_environment::CollisionOperationsGenerator::CollidingJointValues> in_collision_joint_values;

    ops_gen_->generateAlwaysInCollisionPairs(always_in_collision, in_collision_joint_values);

    lock_.lock();
    robot_state_->setKinematicStateToDefault();
    
    std_msgs::ColorRGBA always_color;
    always_color.a = 1.0;
    always_color.r = 1.0;
    always_color.g = .8;
    always_color.b = 0.04;

    collision_markers_.markers.clear();
    cm_->getAllCollisionPointMarkers(*robot_state_,
                                     collision_markers_,
                                     always_color,
                                     ros::Duration(.2));
    clear();
    refresh();
    printw("These pairs (with yellow collision markers) are always in collision.  Collisions will be disabled.\n");
    printw("Press any key to continue.\n");
    refresh();
    lock_.unlock();
    getch();

    ops_gen_->disablePairCollisionChecking(always_in_collision);
    disable_map_[planning_environment::CollisionOperationsGenerator::ALWAYS] = always_in_collision;
    std::vector<planning_environment::CollisionOperationsGenerator::StringPair> default_in_collision;
    ops_gen_->generateDefaultInCollisionPairs(default_in_collision, in_collision_joint_values);

    std_msgs::ColorRGBA default_color;
    default_color.a = 1.0;
    default_color.r = 0.0;
    default_color.g = .8;
    default_color.b = 0.04;
    
    std::vector<double> percentages(default_in_collision.size(), 1.0);
    clear();
    refresh();
    printw("These pairs (with green collision markers) are in collision in the default state.  Collisions will be optionally disabled.\n");
    considerInCollisionPairs(default_in_collision,
                             percentages,
                             in_collision_joint_values,
                             default_color);
    disable_map_[planning_environment::CollisionOperationsGenerator::DEFAULT] = default_in_collision;

  }

  void considerOftenInCollisionPairs() {

    std::vector<planning_environment::CollisionOperationsGenerator::StringPair> often_in_collision;
    std::vector<double> percentages;
    std::vector<planning_environment::CollisionOperationsGenerator::CollidingJointValues> in_collision_joint_values;

    ops_gen_->generateOftenInCollisionPairs(often_in_collision, percentages, in_collision_joint_values);

    if(often_in_collision.size() == 0) {
      printw("No additional often in collision pairs\n");
      refresh();
      return;
    }

    std_msgs::ColorRGBA color;
    color.a = 1.0;
    color.r = 1.0;
    color.g = 0.0;
    color.b = 1.0;

    clear();
    refresh();
    printw("These pairs (with magenta collision markers) are often in collision.  Collisions will be optionally disabled.\n");


    considerInCollisionPairs(often_in_collision,
                             percentages,
                             in_collision_joint_values,
                             color);
    disable_map_[planning_environment::CollisionOperationsGenerator::OFTEN] = often_in_collision;
  }

  void considerOccasionallyInCollisionPairs() {

    std::vector<planning_environment::CollisionOperationsGenerator::StringPair> in_collision;
    std::vector<planning_environment::CollisionOperationsGenerator::StringPair> not_in_collision;
    std::vector<double> percentages;
    std::vector<planning_environment::CollisionOperationsGenerator::CollidingJointValues> in_collision_joint_values;

    ops_gen_->generateOccasionallyAndNeverInCollisionPairs(in_collision, not_in_collision, percentages, in_collision_joint_values);

    if(in_collision.size() == 0) {
      printw("No additional often in collision pairs");
      refresh();
      return;
    }

    std_msgs::ColorRGBA color;
    color.a = 1.0;
    color.r = 1.0;
    color.g = 0.0;
    color.b = 1.0;

    clear();
    refresh();
    //printw("These pairs (with magenta collision markers) are ooccasionally in collision.  Collisions will be optionally disabled.\n");

    // considerInCollisionPairs(in_collision,
    //                          percentages,
    //                          in_collision_joint_values,
    //                          color);

    //disable_map_[planning_environment::CollisionOperationsGenerator::OCCASIONALLY] = in_collision;
    disable_map_[planning_environment::CollisionOperationsGenerator::NEVER] = not_in_collision;

  }
  
  void considerInCollisionPairs(std::vector<planning_environment::CollisionOperationsGenerator::StringPair>& in_collision_pairs,
                                std::vector<double>& percentages,
                                std::vector<planning_environment::CollisionOperationsGenerator::CollidingJointValues>& in_collision_joint_values,
                                const std_msgs::ColorRGBA& color
) {
    std::vector<planning_environment::CollisionOperationsGenerator::StringPair> actually_disabling;
    for(unsigned int i = 0; i < in_collision_pairs.size(); i++) {
      lock_.lock();
      collision_markers_.markers.clear();
      robot_state_->setKinematicState(in_collision_joint_values[i]);
      if(!cm_->isKinematicStateInCollision(*robot_state_)) {
        ROS_INFO_STREAM("Really should be in collision");
      }
      std::vector<collision_space::EnvironmentModel::AllowedContact> allowed_contacts;
      std::vector<collision_space::EnvironmentModel::Contact> coll_space_contacts;
      cm_->getCollisionSpace()->getAllCollisionContacts(allowed_contacts,
                                                        coll_space_contacts,
                                                        1);
      bool found = false;
      visualization_msgs::Marker marker;
      for(unsigned int j = 0; j < coll_space_contacts.size(); j++) {

        if((coll_space_contacts[j].body_name_1 == in_collision_pairs[i].first &&
            coll_space_contacts[j].body_name_2 == in_collision_pairs[i].second) ||
           (coll_space_contacts[j].body_name_1 == in_collision_pairs[i].second &&
            coll_space_contacts[j].body_name_2 == in_collision_pairs[i].first)) {
          found = true;
          marker = transformEnvironmentModelContactInfoMarker(coll_space_contacts[j]);
          marker.color = color;
          marker.lifetime = ros::Duration(.2);
          collision_markers_.markers.push_back(marker);
        }
      }
      lock_.unlock();
      if(!found) { 
        ROS_WARN_STREAM("Collision that should be there not found between " << in_collision_pairs[i].first << " and " << in_collision_pairs[i].second
                        << " " << cm_->isKinematicStateInCollision(*robot_state_));
        for(unsigned int j = 0; j < coll_space_contacts.size(); j++) {
          ROS_INFO_STREAM("Contacts between " << coll_space_contacts[j].body_name_1 << " and " << coll_space_contacts[j].body_name_2);
        }
      } else {
        printw("Disable all collisions between %s and %s (frequency in collision %g) (y or n)?", in_collision_pairs[i].first.c_str(), in_collision_pairs[i].second.c_str(), percentages[i]);
        refresh();
        char str[80];
        getstr(str);
        if(str[0] != 'n') {
          ops_gen_->disablePairCollisionChecking(in_collision_pairs[i]);
          actually_disabling.push_back(in_collision_pairs[i]);
        }
      }
    }
    in_collision_pairs = actually_disabling;
  }
 
  void outputPlanningDescriptionYAML() {
    //initial map
    emitter_ << YAML::BeginMap;
    emitWorldJointYAML();
    emitGroupYAML();
    //ops_gen_->performanceTestSavedResults(disable_map_);
    ops_gen_->outputYamlStringOfSavedResults(emitter_, disable_map_);
    //end map
    emitter_ << YAML::EndMap;
    std::ofstream outf(full_yaml_outfile_name_.c_str(), std::ios_base::trunc);
    
    outf << emitter_.c_str();
  }

  void outputOMPLGroupYAML() {
    YAML::Emitter emitter;
    emitter << YAML::BeginMap;
    emitter << YAML::Key << "planner_configs";
    
    emitter << YAML::Value << YAML::BeginMap;

    emitter << YAML::Key << "SBLkConfig1";
    emitter << YAML::Value << YAML::BeginMap;
    emitter << YAML::Key << "type" << YAML::Value << "kinematic::SBL";
    emitter << YAML::EndMap;

    emitter << YAML::Key << "LBKPIECEkConfig1";
    emitter << YAML::Value << YAML::BeginMap;
    emitter << YAML::Key << "type" << YAML::Value << "kinematic::LBKPIECE";
    emitter << YAML::EndMap;

    emitter << YAML::EndMap;

    emitter << YAML::Key << "groups";
    const std::map<std::string, planning_models::KinematicModel::GroupConfig>& group_config_map = kmodel_->getJointModelGroupConfigMap();

    emitter << YAML::Value << YAML::BeginSeq; 
    for(std::map<std::string, planning_models::KinematicModel::GroupConfig>::const_iterator it = group_config_map.begin();
        it != group_config_map.end();
        it++) {
      emitter << it->first;
    }
    emitter << YAML::EndSeq;

    for(std::map<std::string, planning_models::KinematicModel::GroupConfig>::const_iterator it = group_config_map.begin();
        it != group_config_map.end();
        it++) {
      emitter << YAML::Key << it->first;
      emitter << YAML::Value << YAML::BeginMap;
      emitter << YAML::Key << "planner_type" << YAML::Value << "JointPlanner";
      emitter << YAML::Key << "planner_configs" << YAML::Value << YAML::BeginSeq;
      emitter << "SBLkConfig1" << "LBKPIECEkConfig1" << YAML::EndSeq;
      emitter << YAML::Key << "projection_evaluator" << YAML::Value << "joint_state";
      emitter << YAML::EndMap;
    }
    emitter << YAML::EndMap;
    std::ofstream outf((dir_name_+"/config/ompl_planning.yaml").c_str(), std::ios_base::trunc);
    
    outf << emitter.c_str();    
  }

  void outputOMPLLaunchFile() {
    TiXmlDocument doc;
    TiXmlElement* launch_root = new TiXmlElement("launch");
    doc.LinkEndChild(launch_root);
    
    TiXmlElement *inc = new TiXmlElement("include");
    launch_root->LinkEndChild(inc);
    inc->SetAttribute("file","$(find "+dir_name_+")/launch/"+launch_outfile_name_);

    TiXmlElement *node = new TiXmlElement("node");
    launch_root->LinkEndChild(node);
    node->SetAttribute("pkg","ompl_ros_interface");
    node->SetAttribute("type", "ompl_ros");
    node->SetAttribute("name", "ompl_planning");

    TiXmlElement *rp = new TiXmlElement("rosparam");
    node->LinkEndChild(rp);
    rp->SetAttribute("command","load");
    rp->SetAttribute("file", "$(find "+dir_name_+")/config/ompl_planning.yaml");
    doc.SaveFile(dir_name_+"/launch/ompl_planning.launch");
  }

  void outputTrajectoryFilterLaunch() {
    TiXmlDocument doc;
    TiXmlElement* launch_root = new TiXmlElement("launch");
    doc.LinkEndChild(launch_root);
    
    TiXmlElement *inc = new TiXmlElement("include");
    launch_root->LinkEndChild(inc);
    inc->SetAttribute("file","$(find "+dir_name_+")/launch/"+launch_outfile_name_);

    TiXmlElement *node = new TiXmlElement("node");
    launch_root->LinkEndChild(node);
    node->SetAttribute("pkg","trajectory_filter_server");
    node->SetAttribute("type","trajectory_filter_server");
    node->SetAttribute("name", "trajectory_filter_server");

    TiXmlElement *rp = new TiXmlElement("rosparam");
    node->LinkEndChild(rp);
    rp->SetAttribute("command","load");
    rp->SetAttribute("file", "$(find trajectory_filter_server)/config/filters.yaml");

    TiXmlElement *rp2 = new TiXmlElement("rosparam");
    node->LinkEndChild(rp2);
    rp2->SetAttribute("command","load");
    rp2->SetAttribute("file", "$(find "+dir_name_+")/config/joint_limits.yaml");

    doc.SaveFile(dir_name_+"/launch/trajectory_filter_server.launch");
  }

  void outputPlanningEnvironmentLaunch() {
    TiXmlDocument doc;
    TiXmlElement* launch_root = new TiXmlElement("launch");
    doc.LinkEndChild(launch_root);

    TiXmlElement *rd = new TiXmlElement("rosparam"); 
    launch_root->LinkEndChild(rd);
    rd->SetAttribute("command","load");
    rd->SetAttribute("ns", "robot_description");
    rd->SetAttribute("file", "$(find "+urdf_package_+")"+urdf_path_);

    TiXmlElement *rp = new TiXmlElement("rosparam");
    launch_root->LinkEndChild(rp);
    rp->SetAttribute("command","load");
    rp->SetAttribute("ns", "robot_description_planning");
    rp->SetAttribute("file", "$(find "+dir_name_+")/config/"+yaml_outfile_name_);
    doc.SaveFile(full_launch_outfile_name_);

  }
  
  void outputKinematicsLaunchFiles() {
    TiXmlDocument doc;
    TiXmlElement* launch_root = new TiXmlElement("launch");
    doc.LinkEndChild(launch_root);
    
    TiXmlElement *inc = new TiXmlElement("include");
    launch_root->LinkEndChild(inc);
    inc->SetAttribute("file","$(find "+dir_name_+")/launch/"+launch_outfile_name_);

    const std::map<std::string, planning_models::KinematicModel::GroupConfig>& group_config_map = kmodel_->getJointModelGroupConfigMap();

    for(std::map<std::string, planning_models::KinematicModel::GroupConfig>::const_iterator it = group_config_map.begin();
        it != group_config_map.end();
        it++) {
      if(!it->second.base_link_.empty()) {
        TiXmlElement *node = new TiXmlElement("node");
        launch_root->LinkEndChild(node);
        node->SetAttribute("pkg","arm_kinematics_constraint_aware");
        node->SetAttribute("type","arm_kinematics_constraint_aware");
        node->SetAttribute("name", getRobotName()+"_"+it->first+"_kinematics");

        TiXmlElement *group_param = new TiXmlElement("param");
        node->LinkEndChild(group_param);
        group_param->SetAttribute("name", "group");
        group_param->SetAttribute("type", "string");
        group_param->SetAttribute("value", it->first);

        TiXmlElement *base_param = new TiXmlElement("param");
        node->LinkEndChild(base_param);
        base_param->SetAttribute("name", it->first+"/root_name");
        base_param->SetAttribute("type", "string");
        base_param->SetAttribute("value", it->second.base_link_);

        TiXmlElement *tip_param = new TiXmlElement("param");
        node->LinkEndChild(tip_param);
        tip_param->SetAttribute("name", it->first+"/tip_name");
        tip_param->SetAttribute("type", "string");
        tip_param->SetAttribute("value", it->second.tip_link_);

        TiXmlElement *solver_param = new TiXmlElement("param");
        node->LinkEndChild(solver_param);
        solver_param->SetAttribute("name", "kinematics_solver");
        solver_param->SetAttribute("type", "string");
        solver_param->SetAttribute("value", "arm_kinematics_constraint_aware/KDLArmKinematicsPlugin");
      }
    }


    doc.SaveFile(dir_name_+"/launch/constraint_aware_kinematics.launch");
  }

  void outputPlanningComponentVisualizerLaunchFile() {
    TiXmlDocument doc;
    TiXmlElement* launch_root = new TiXmlElement("launch");
    doc.LinkEndChild(launch_root);
    
    TiXmlElement *inc = new TiXmlElement("include");
    launch_root->LinkEndChild(inc);
    inc->SetAttribute("file","$(find "+dir_name_+")/launch/"+launch_outfile_name_);

    TiXmlElement *pre = new TiXmlElement("include");
    launch_root->LinkEndChild(pre);
    pre->SetAttribute("file","$(find planning_environment)/launch/planning_environment_visualization_prerequisites.launch");

    TiXmlElement *kin = new TiXmlElement("include");
    launch_root->LinkEndChild(kin);
    kin->SetAttribute("file", "$(find "+dir_name_+")/launch/constraint_aware_kinematics.launch");

    TiXmlElement *ompl = new TiXmlElement("include");
    launch_root->LinkEndChild(ompl);
    ompl->SetAttribute("file", "$(find "+dir_name_+")/launch/ompl_planning.launch");

    TiXmlElement *fil = new TiXmlElement("include");
    launch_root->LinkEndChild(fil);
    fil->SetAttribute("file", "$(find "+dir_name_+")/launch/trajectory_filter_server.launch");

    TiXmlElement *vis = new TiXmlElement("node");
    launch_root->LinkEndChild(vis);
    vis->SetAttribute("pkg","move_arm");
    vis->SetAttribute("type","planning_components_visualizer");
    vis->SetAttribute("name","planning_components_visualizer"); 
    vis->SetAttribute("output","screen");

    TiXmlElement *state_publisher = new TiXmlElement("node");
    launch_root->LinkEndChild(state_publisher);
    state_publisher->SetAttribute("pkg","robot_state_publisher" );
    state_publisher->SetAttribute("type", "state_publisher");
    state_publisher->SetAttribute("name", "rob_st_pub");

    doc.SaveFile(dir_name_+"/launch/planning_components_visualizer.launch");
  }

  void updateCollisionsInCurrentState() {
    lock_.lock();
    std_msgs::ColorRGBA default_color;
    default_color.a = 1.0;
    default_color.r = 0.0;
    default_color.g = .8;
    default_color.b = 0.04;

    collision_markers_.markers.clear();
    
    cm_->getAllCollisionPointMarkers(*robot_state_,
                                     collision_markers_,
                                     default_color,
                                     ros::Duration(.2));
    lock_.unlock();
  }

  void sendMarkers() 
  {
    lock_.lock();
    vis_marker_array_publisher_.publish(collision_markers_);
    if(!current_show_group_.empty()) {
      visualization_msgs::MarkerArray arr;
      std_msgs::ColorRGBA default_color;
      default_color.a = 1.0;
      default_color.r = 0.0;
      default_color.g = .8;
      default_color.b = 0.04;

      std_msgs::ColorRGBA color;
      color.a = 1.0;
      color.r = 1.0;
      color.g = 0.0;
      color.b = 1.0;

      const planning_models::KinematicModel::JointModelGroup* jmg = kmodel_->getModelGroup(current_show_group_);

      std::vector<std::string> group_link_names = jmg->getGroupLinkNames();      
      getRobotMeshResourceMarkersGivenState(*robot_state_,
                                            arr,
                                            default_color,
                                            current_show_group_,
                                            ros::Duration(.2),
                                            &group_link_names);
      
      std::vector<std::string> updated_link_model_names = jmg->getUpdatedLinkModelNames();
      std::map<std::string, bool> dont_include;
      for(unsigned int i = 0; i < group_link_names.size(); i++) {
        dont_include[group_link_names[i]] = true;
      }

      std::vector<std::string> ex_list;
      for(unsigned int i = 0; i < updated_link_model_names.size(); i++) {
        if(dont_include.find(updated_link_model_names[i]) == dont_include.end()) {
          ex_list.push_back(updated_link_model_names[i]);
        }
      }
      //first n will be actually in group
      getRobotMeshResourceMarkersGivenState(*robot_state_,
                                            arr,
                                            color,
                                            current_show_group_+"_updated_links",
                                            ros::Duration(.2),
                                            &ex_list);
      vis_marker_array_publisher_.publish(arr);
    }
    lock_.unlock();
  }

  void sendTransforms() {
    lock_.lock();
    ros::WallTime cur_time = ros::WallTime::now();
    rosgraph_msgs::Clock c;
    c.clock.nsec = cur_time.nsec;
    c.clock.sec = cur_time.sec;
    std::vector<geometry_msgs::TransformStamped> trans_vector;
    planning_environment::getAllKinematicStateStampedTransforms(*robot_state_, trans_vector, c.clock);
    transform_broadcaster_.sendTransform(trans_vector);
    lock_.unlock();
  };

  bool isInited() const {
    return inited_;
  }

  planning_environment::CollisionOperationsGenerator* getOperationsGenerator() 
  { 
    return ops_gen_;
  }

  std::string getRobotName() {
    return urdf_->getName();
  }
  
  void getRobotMeshResourceMarkersGivenState(const planning_models::KinematicState& state,
                                             visualization_msgs::MarkerArray& arr,
                                             const std_msgs::ColorRGBA& color,
                                             const std::string& name, 
                                             const ros::Duration& lifetime,
                                             const std::vector<std::string>* names) const
  {  
    boost::shared_ptr<urdf::Model> robot_model = urdf_;
    
    std::vector<std::string> link_names;
    if(names == NULL) {
      kmodel_->getLinkModelNames(link_names);
    } else {
      link_names = *names;
    }
    
    for(unsigned int i = 0; i < link_names.size(); i++) {
      boost::shared_ptr<const urdf::Link> urdf_link = robot_model->getLink(link_names[i]);
      if(!urdf_link) {
        ROS_INFO_STREAM("Invalid urdf name " << link_names[i]);
        continue;
      }
      if(!urdf_link->collision) {
        continue;
      }
      const urdf::Geometry *geom = urdf_link->collision->geometry.get();
      if(!geom) {
        continue;
      }
      const urdf::Mesh *mesh = dynamic_cast<const urdf::Mesh*>(geom);
      if(!mesh) {
        continue;
      }
      if(mesh->filename.empty()) {
        continue;
      }
      const planning_models::KinematicState::LinkState* ls = state.getLinkState(link_names[i]);
      if(ls == NULL) {
        ROS_WARN_STREAM("No link state for name " << names << " though there's a mesh");
        continue;
      }
      visualization_msgs::Marker mark;
      mark.header.frame_id = kmodel_->getRoot()->getParentFrameId();
      mark.header.stamp = ros::Time::now();
      mark.ns = name;
      mark.id = i;
      mark.type = mark.MESH_RESOURCE;
      mark.scale.x = 1.0;
      mark.scale.y = 1.0;
      mark.scale.z = 1.0;
      mark.color = color;
      mark.mesh_resource = mesh->filename;
      mark.lifetime = lifetime;
      tf::poseTFToMsg(ls->getGlobalCollisionBodyTransform(),mark.pose); 
      arr.markers.push_back(mark);
    }
  }

protected:

  bool inited_;

  ros::NodeHandle nh_;
  boost::shared_ptr<urdf::Model> urdf_;  
  planning_models::KinematicModel* kmodel_;
  planning_environment::CollisionModels* cm_;
  planning_environment::CollisionOperationsGenerator* ops_gen_;
  planning_models::KinematicState* robot_state_;
  collision_space::EnvironmentModel* ode_collision_model_;
  visualization_msgs::MarkerArray collision_markers_;
  planning_models::KinematicModel::MultiDofConfig world_joint_config_;
  std::map<planning_environment::CollisionOperationsGenerator::DisableType, std::vector<planning_environment::CollisionOperationsGenerator::StringPair> > disable_map_;

  std::string current_show_group_;

  tf::TransformBroadcaster transform_broadcaster_;
  ros::Publisher vis_marker_publisher_;
  ros::Publisher vis_marker_array_publisher_;
  
  boost::recursive_mutex lock_;

  YAML::Emitter emitter_;

  std::string dir_name_;
  std::string yaml_outfile_name_, full_yaml_outfile_name_;
  std::string launch_outfile_name_, full_launch_outfile_name_;

  std::string urdf_package_, urdf_path_;

};

PlanningDescriptionConfigurationWizard* pdcw;

bool inited = false;

void spin_function() {
  ros::WallRate r(100.0);
  unsigned int counter = 0;
  while(ros::ok()) {
    if(inited) {
      pdcw->sendTransforms();
      if(counter%CONTROL_SPEED == 0) {
        counter = 1;
        pdcw->sendMarkers();
      } else {
        counter++;
      }
    }
    r.sleep();
    ros::spinOnce();
  }
}

void quit(int sig)
{
  if(pdcw != NULL) {
    delete pdcw;
  }
  endwin();
  exit(0);
}

int main(int argc, char** argv)
{

  srand(time(NULL));
  ros::init(argc, argv, "planning_description_configuration_wizard", ros::init_options::NoSigintHandler);

  if(argc < 3) {
    ROS_INFO_STREAM("Must specify a package and relative urdf file");
    exit(0);
  }

  std::string urdf_package = argv[1];
  std::string urdf_path = argv[2];
  pdcw = new PlanningDescriptionConfigurationWizard(urdf_package, urdf_path);

  if(!pdcw->isInited()) {
    ROS_WARN_STREAM("Can't init. Exiting");
    exit(0);
  }

  inited = true;

  boost::thread spin_thread(boost::bind(&spin_function));

  initscr();
  use_default_colors();
  start_color();

  pdcw->outputPlanningEnvironmentLaunch();
  
  pdcw->setupGroups();

  pdcw->outputKinematicsLaunchFiles();
  pdcw->outputPlanningComponentVisualizerLaunchFile();
  pdcw->outputOMPLGroupYAML();
  pdcw->outputOMPLLaunchFile();
  pdcw->outputTrajectoryFilterLaunch();
  pdcw->outputJointLimitsYAML();

  pdcw->setJointsForCollisionSampling();

  pdcw->considerAlwaysAndDefaultInCollisionMarkers();

  printw("Finding often in collision pairs\n");
  refresh();
  pdcw->considerOftenInCollisionPairs();

  printw("Finding occasionally in collision pairs\n");
  refresh();
  pdcw->considerOccasionallyInCollisionPairs();

  printw("Performance testing and writing to file\n");
  refresh();
  pdcw->outputPlanningDescriptionYAML();
  printw("Press any key to exit\n");
  refresh();
  getch();
  endwin();
  ros::shutdown();
  return 0;
}





  

