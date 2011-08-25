/*
 * Copyright (c) 2008, Willow Garage, Inc.
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
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
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

/*
 * Author: Sachin Chitta
 */
#ifndef MULTI_ARM_KINEMATICS_CONSTRAINT_AWARE_H_
#define MULTI_ARM_KINEMATICS_CONSTRAINT_AWARE_H_

#include <arm_kinematics_constraint_aware/multi_arm_kinematics_exception.h>

// System
#include <boost/shared_ptr.hpp>

// ROS msgs

#include <arm_navigation_msgs/OrderedCollisionOperations.h>
#include <arm_navigation_msgs/ArmNavigationErrorCodes.h>
#include <arm_navigation_msgs/DisplayTrajectory.h>
#include <arm_navigation_msgs/PlanningScene.h>
#include <arm_navigation_msgs/Constraints.h>
#include <arm_navigation_msgs/LinkPadding.h>
#include <arm_navigation_msgs/JointLimits.h>

// MISC
#include <planning_environment/models/collision_models.h>
#include <arm_kinematics_constraint_aware/arm_kinematics_constraint_aware_utils.h>
#include <kdl/jntarray.hpp>
#include <angles/angles.h>
#include <urdf/model.h>
#include <spline_smoother/linear_trajectory.h>

// plugin
#include <pluginlib/class_loader.h>
#include <kinematics_base/kinematics_base.h>

namespace arm_kinematics_constraint_aware {

class MultiArmKinematicsConstraintAware{

public:
  MultiArmKinematicsConstraintAware(const std::vector<std::string> &group_names, 
                                    const std::vector<std::string> &kinematics_solver_names,
                                    const std::vector<std::string> &end_effector_link_names);

  MultiArmKinematicsConstraintAware(const std::vector<std::string> &group_names, 
                                    const std::vector<std::string> &kinematics_solver_names,
                                    const std::vector<std::string> &end_effector_link_names,
                                    planning_environment::CollisionModels *collision_models);

  ~MultiArmKinematicsConstraintAware()
  {
    if(collision_models_generated_)
      delete collision_models_;
  }

  bool setup(const arm_navigation_msgs::PlanningScene& planning_scene,
             const arm_navigation_msgs::OrderedCollisionOperations &collision_operations);
  
  bool clear();
  

  bool getPositionIK(const std::vector<geometry_msgs::Pose> &poses,
                     const std::vector<std::vector<double> > &seed_states,
                     double &timeout,
                     std::vector<std::vector<double> > &solutions,
                     std::vector<int> &error_codes);

  bool checkJointStates(const std::map<std::string, double> &joint_values,
                        const arm_navigation_msgs::Constraints &constraints,
                        double &timeout,
                        planning_models::KinematicState *kinematic_state,
                        int &error_code);
  
  bool checkJointStates(const std::vector<double>  &solutions,
                        const arm_navigation_msgs::Constraints &constraints,
                        double &timeout,
                        planning_models::KinematicState *kinematic_state,
                        int &error_code);

  bool checkJointStates(const std::vector<std::vector<double> > &solutions,
                        const arm_navigation_msgs::Constraints &constraints,
                        double &timeout,
                        planning_models::KinematicState *kinematic_state,
                        int &error_code);

  bool checkMotion(const std::vector<std::vector<double> > &start,
                   const std::vector<std::vector<double> > &end,
                   const arm_navigation_msgs::Constraints &constraints,
                   double &timeout,
                   planning_models::KinematicState *kinematic_state,
                   int &error_code);

  bool checkEndEffectorStates(const std::vector<geometry_msgs::Pose> &poses,
                              double &timeout,
                              planning_models::KinematicState *kinematic_state,
                              std::vector<int> &error_codes);
  
  bool searchPositionIK(const std::vector<geometry_msgs::Pose> &poses,
                        const std::vector<std::vector<double> > &seed_states,
                        double &timeout,
                        std::vector<std::vector<double> > &solutions,
                        std::vector<int> &error_codes);

  std::vector<std::string> getArmNames();

  std::vector<std::string> getEndEffectorNames();

  std::vector<std::string> getJointNames();

  std::vector<std::vector<std::string> > getJointNamesByGroup();

  bool searchConstraintAwarePositionIK(const std::vector<geometry_msgs::Pose> &poses,
                                       const std::vector<std::vector<double> > &seed_states,
                                       double &timeout,
                                       planning_models::KinematicState *kinematic_state,
                                       std::vector<std::vector<double> > &solutions,
                                       std::vector<int> &error_codes,
                                       const double &max_distance=0.0);
  bool searchConstraintAwarePositionIK(const std::vector<geometry_msgs::Pose> &poses,
                                       double &timeout,
                                       planning_models::KinematicState *kinematic_state,
                                       std::vector<std::vector<double> > &solutions,
                                       std::vector<int> &error_codes,
                                       const double &max_distance=0.0);  
  bool searchConstraintAwarePositionIK(const std::vector<geometry_msgs::Pose> &poses,
                                       const arm_navigation_msgs::Constraints &constraints,
                                       double &timeout,
                                       planning_models::KinematicState *kinematic_state,
                                       std::vector<std::vector<double> > &solutions,
                                       std::vector<int> &error_codes,
                                       const double &max_distance=0.0);
  bool searchConstraintAwarePositionIK(const std::vector<geometry_msgs::Pose> &poses,
                                       const std::vector<std::vector<double> > &seed_states,
                                       const arm_navigation_msgs::Constraints &constraints,
                                       double &timeout,
                                       planning_models::KinematicState *kinematic_state,
                                       std::vector<std::vector<double> > &solutions,
                                       std::vector<int> &error_codes,
                                       const double &max_distance=0.0);
  std::string getBaseFrame(){return base_frame_;};
private:

  spline_smoother::LinearTrajectory linear_trajectory_solver_;
  std::vector<arm_navigation_msgs::JointLimits> joint_limits_;//only used for linear trajectory, does not contain right velocity limits;
  trajectory_msgs::JointTrajectory linear_trajectory_;
  std::vector<double> linear_trajectory_waypoint_times_;

  bool initialize(const std::vector<std::string> &group_names, 
                  const std::vector<std::string> &kinematics_solver_names,
                  const std::vector<std::string> &end_effector_link_names,
                  planning_environment::CollisionModels *collision_models);

  bool checkRequest(const std::vector<geometry_msgs::Pose> &poses,
                    const std::vector<std::vector<double> > &seed_states,
                    std::vector<std::vector<double> > &solutions,
                    std::vector<int> &error_codes);
  

  void sendEndEffectorPoseToVisualizer(const planning_models::KinematicState* state, bool valid);
  void generateRandomState(std::vector<std::vector<double> > &state);
  double generateRandomNumber(const double &min, const double &max);

  unsigned int num_groups_;
  std::vector<kinematics::KinematicsBase*> kinematics_solvers_;
  pluginlib::ClassLoader<kinematics::KinematicsBase> kinematics_loader_;
  std::vector<std::string> group_names_, arm_links_, kinematics_solver_names_, end_effector_link_names_;
  std::vector<std::vector<std::string> >  end_effector_collision_links_;
  std::string base_frame_;
  planning_models::KinematicState* original_state_;
  planning_environment::CollisionModels *collision_models_;

  std::vector<std::vector<std::pair<double,double> > > bounds_;
  bool collision_models_generated_;

  unsigned int total_num_joints_;
  double collision_discretization_;
};
} // namespace

#endif