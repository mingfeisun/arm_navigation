/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2009, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Willow Garage nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/** \author Mrinal Kalakrishnan */

#include <arm_navigation_msgs/FilterJointTrajectory.h>
#include <arm_navigation_msgs/FilterJointTrajectoryWithConstraints.h>
#include <joint_normalization_filters/normalize_joint_trajectory.h>

PLUGINLIB_REGISTER_CLASS(NormalizeFilterJointTrajectoryWithConstraints,joint_normalization_filters::NormalizeJointTrajectory<arm_navigation_msgs::FilterJointTrajectoryWithConstraints::Request>,filters::FilterBase<arm_navigation_msgs::FilterJointTrajectoryWithConstraints::Request>)
PLUGINLIB_REGISTER_CLASS(NormalizeFilterJointTrajectory,joint_normalization_filters::NormalizeJointTrajectory<arm_navigation_msgs::FilterJointTrajectory::Request>,filters::FilterBase<arm_navigation_msgs::FilterJointTrajectory::Request>)
PLUGINLIB_REGISTER_CLASS(NormalizeJointTrajectoryWithLimits,joint_normalization_filters::NormalizeJointTrajectory<arm_navigation_msgs::JointTrajectoryWithLimits>,filters::FilterBase<arm_navigation_msgs::JointTrajectoryWithLimits>)

