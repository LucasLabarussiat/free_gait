/*
 * Executor.cpp
 *
 *  Created on: Oct 20, 2015
 *      Author: Péter Fankhauser
 *   Institute: ETH Zurich, Autonomous Systems Lab
 */

#include "free_gait_core/executor/Executor.hpp"

namespace free_gait {

Executor::Executor(std::shared_ptr<StepCompleter> completer,
                   std::shared_ptr<AdapterBase> adapter,
                   std::shared_ptr<State> state)
    : completer_(completer),
      adapter_(adapter),
      state_(state),
      isInitialized_(false)
{
  // TODO Auto-generated constructor stub

}

Executor::~Executor()
{
  // TODO Auto-generated destructor stub
}

bool Executor::initialize()
{
  state_->initialize(adapter_->getLimbs(), adapter_->getBranches());
  reset();
  return isInitialized_ = true;
}

bool Executor::isInitialized() const
{
  return isInitialized_;
}

bool Executor::advance(double dt)
{
  if (!isInitialized_) return false;

  if (checkRobotStatus()) {
    if (!state_->getRobotExecutionStatus()) std::cout << "Continuing with free gait execution." << std::endl;
    state_->setRobotExecutionStatus(true);
  } else {
    if (state_->getRobotExecutionStatus()) std::cout << "Robot status is not OK, not continuing with free gait execution." << std::endl;
    state_->setRobotExecutionStatus(false);
    return true;
  }

  bool hasSwitchedStep;
  if (!queue_.advance(dt, hasSwitchedStep)) return false;
  if (queue_.empty()) return true;

  while (hasSwitchedStep) {
    updateStateWithMeasurements();
    completer_->complete(*state_, queue_, queue_.getCurrentStep());

    if (hasSwitchedStep) {
      std::cout << "Switched step state to:" << std::endl;
      std::cout << queue_.getCurrentStep() << std::endl;
    }

    if (!queue_.advance(dt, hasSwitchedStep)) return false; // Advance again after completion.
    if (queue_.empty()) return true;
  }

  if (!writeIgnoreContact()) return false;
  if (!writeSupportLegs()) return false;
  if (!writeSurfaceNormals()) return false;
  if (!writeLegMotion()) return false;
  if (!writeTorsoMotion()) return false;
  if (!adapter_->updateExtras(queue_, *state_)) return false;

//  if (queue_.hasSwitchedStep() && !queue_.getCurrentStep()->isComplete()) {
//    completer_->complete(*queue_.getCurrentStep());
//    // Update actuation type and robot state.
//    if (!queue_.getPreviousStep()) {
//      updateWithMeasuredBasePose();
//      updateWithMeasuredBaseTwist();
//      for (const auto& legControlSetup : legControlSetups_) {
//        updateWithMeasuredJointPositions(legControlSetup.first);
//        updateWithMeasuredJointVelocities(legControlSetup.first);
//        updateWithMeasuredJointEfforts(legControlSetup.first);
//        updateWithActualSupportLeg(legControlSetup.first);
//      }
//    } else {
//      auto& previousBaseSetup = queue_.getPreviousStep()->getCurrentBaseMotion().getControlSetup();
//      if (!previousBaseSetup.at(ControlLevel::Position)) updateWithMeasuredBasePose();
//      if (!previousBaseSetup.at(ControlLevel::Velocity)) updateWithMeasuredBaseTwist();
//
//      for (const auto& legControlSetup : legControlSetups_) {
//        // TODO: Special treatment for non-specified legs.
//        auto& previousLegSetup = queue_.getPreviousStep()->getLegMotions().at(legControlSetup.first).getControlSetup();
//        if (!previousBaseSetup.at(ControlLevel::Position)) updateWithMeasuredJointPositions(legControlSetup.first);
//        if (!previousBaseSetup.at(ControlLevel::Velocity)) updateWithMeasuredJointVelocities(legControlSetup.first);
//        // TODO etc.!!!
//      }
//    }
//
//    baseControlSetup_ = queue_.getCurrentStep()->getCurrentBaseMotion().getControlSetup();
//    for (const auto& legMotion : queue_.getCurrentStep()->getLegMotions()) {
//      legControlSetups_.at(legMotion.first) = legMotion.second.getControlSetup();
//    }
//  }
//
//  auto step = queue_.getCurrentStep();
//  // TODO Do this with all frame handling.
//  if (baseControlSetup_.at(ControlLevel::Position)) basePose_ = step->getCurrentBaseMotion().evaluatePose(step->getCurrentStateTime());
//  return true;

  return true;
}

void Executor::reset()
{
  queue_.clear();
  initializeStateWithRobot();
}

const StepQueue& Executor::getQueue() const
{
  return queue_;
}

StepQueue& Executor::getQueue()
{
  return queue_;
}

const State& Executor::getState() const
{
  return *state_;
}

const AdapterBase& Executor::getAdapter() const
{
  return *adapter_;
}

bool Executor::checkRobotStatus()
{
  for (const auto& limb : adapter_->getLimbs()) {
    if (state_->isSupportLeg(limb) && !adapter_->isLegGrounded(limb)) return false;
  }
  return true;
}

bool Executor::initializeStateWithRobot()
{
  for (const auto& limb : adapter_->getLimbs()) {
    state_->setSupportLeg(limb, adapter_->isLegGrounded(limb));
    state_->setIgnoreContact(limb, !adapter_->isLegGrounded(limb));
    state_->setIgnoreForPoseAdaptation(limb, !adapter_->isLegGrounded(limb));
    state_->removeSurfaceNormal(limb);
  }

  if (state_->getNumberOfSupportLegs() > 0) {
    state_->setControlSetup(BranchEnum::BASE, adapter_->getControlSetup(BranchEnum::BASE));
  } else {
    state_->setEmptyControlSetup(BranchEnum::BASE);
  }

  for (const auto& limb : adapter_->getLimbs()) {
    if (state_->isSupportLeg(limb)) {
      state_->setEmptyControlSetup(limb);
    } else {
      state_->setControlSetup(limb, adapter_->getControlSetup(limb));
    }
  }

  state_->setAllJointPositions(adapter_->getAllJointPositions());
  state_->setAllJointVelocities(adapter_->getAllJointVelocities());
  state_->setPositionWorldToBaseInWorldFrame(adapter_->getPositionWorldToBaseInWorldFrame());
  state_->setOrientationWorldToBase(adapter_->getOrientationWorldToBase());

  for (const auto& limb : adapter_->getLimbs()) {
    state_->setSupportLeg(limb, adapter_->isLegGrounded(limb));
    state_->setIgnoreContact(limb, !adapter_->isLegGrounded(limb));
  }

  state_->setRobotExecutionStatus(true);

  return true;
}

bool Executor::updateStateWithMeasurements()
{
  // Update states for new step.
//  if (!queue_.previousStepExists()) {
    // Update all states.

  for (const auto& limb : adapter_->getLimbs()) {
    const auto& controlSetup = state_->getControlSetup(limb);
    if (!controlSetup[ControlLevel::Position])
  }

    state_->setAllJointPositions(adapter_->getAllJointPositions());
    state_->setAllJointVelocities(adapter_->getAllJointVelocities());
    // TODO Copy also acceleraitons and torques.
    state_->setPositionWorldToBaseInWorldFrame(adapter_->getPositionWorldToBaseInWorldFrame());
    state_->setOrientationWorldToBase(adapter_->getOrientationWorldToBase());
//    state.setLinearVelocityBaseInWorldFrame(torso_->getMeasuredState().getLinearVelocityBaseInBaseFrame());
//    state.setAngularVelocityBaseInBaseFrame(torso_->getMeasuredState().getAngularVelocityBaseInBaseFrame());
    return true;

//  } else {
    // Update uncontrolled steps.
//  }







//    JointEfforts jointTorques_;
//    LinearAcceleration linearAccelerationBaseInWorldFrame_;
//    AngularAcceleration angularAccelerationBaseInBaseFrame_;
//
//    // Free gait specific.
//    std::unordered_map<BranchEnum, ControlSetup, EnumClassHash> controlSetups_;
//    Force netForceOnBaseInBaseFrame_;
//    Torque netTorqueOnBaseInBaseFrame_;
//    std::unordered_map<LimbEnum, bool, EnumClassHash> isSupportLegs_;
//    std::unordered_map<LimbEnum, bool, EnumClassHash> ignoreContact_;
//    std::unordered_map<LimbEnum, bool, EnumClassHash> ignoreForPoseAdaptation_;
//    std::unordered_map<LimbEnum, Vector, EnumClassHash> surfaceNormals_;
//    bool robotExecutionStatus_;

  return true;
}

bool Executor::writeIgnoreContact()
{
  const Step& step = queue_.getCurrentStep();
  for (const auto& limb : adapter_->getLimbs()) {
    if (step.hasLegMotion(limb)) {
      bool ignoreContact = step.getLegMotion(limb).isIgnoreContact();
      state_->setIgnoreContact(limb, ignoreContact);
    }
  }
  return true;
}

bool Executor::writeSupportLegs()
{
  const Step& step = queue_.getCurrentStep();
  for (const auto& limb : adapter_->getLimbs()) {
    if (step.hasLegMotion(limb) || state_->isIgnoreContact(limb)) {
      state_->setSupportLeg(limb, false);
    } else {
      state_->setSupportLeg(limb, true);
    }
  }
  return true;
}

bool Executor::writeSurfaceNormals()
{
  const Step& step = queue_.getCurrentStep();
  for (const auto& limb : adapter_->getLimbs()) {
    if (step.hasLegMotion(limb)) {
      if (step.getLegMotion(limb).hasSurfaceNormal()) {
        state_->setSurfaceNormal(limb, step.getLegMotion(limb).getSurfaceNormal());
      }
    }
  }
  return true;
}

bool Executor::writeLegMotion()
{
  for (const auto& limb : adapter_->getLimbs()) {
    if (state_->isSupportLeg(limb)) state_->setEmptyControlSetup(limb);
  }

  const auto& step = queue_.getCurrentStep();
  if (!step.hasLegMotion()) return true;

  double time = queue_.getCurrentStep().getTime();
  for (const auto& limb : adapter_->getLimbs()) {
    if (!step.hasLegMotion(limb)) continue;
    auto const& legMotion = step.getLegMotion(limb);
    ControlSetup controlSetup = legMotion.getControlSetup();
    state_->setControlSetup(limb, controlSetup);

    switch (legMotion.getTrajectoryType()) {

      case LegMotionBase::TrajectoryType::EndEffector:
      {
        const auto& endEffectorMotion = dynamic_cast<const EndEffectorMotionBase&>(legMotion);
        if (controlSetup[ControlLevel::Position]) {
          // TODO Add frame handling.
          Position positionInWorldFrame(endEffectorMotion.evaluatePosition(time));
          Position positionInBaseFrame(adapter_->getOrientationWorldToBase().rotate(positionInWorldFrame - adapter_->getPositionWorldToBaseInWorldFrame()));
          JointPositions jointPositions;
          adapter_->getLimbJointPositionsFromPositionBaseToFootInBaseFrame(positionInBaseFrame, limb, jointPositions);
          state_->setJointPositions(limb, jointPositions);
        }
        break;
      }

      case LegMotionBase::TrajectoryType::Joints:
      {
        const auto& jointMotion = dynamic_cast<const JointMotionBase&>(legMotion);
        if (controlSetup[ControlLevel::Position]) {
          state_->setJointPositions(limb, jointMotion.evaluatePosition(time));
        }
        break;
      }

      default:
        throw std::runtime_error("Executor::writeLegMotion() could not write leg motion of this type.");
        break;
    }
  }
  return true;
}

bool Executor::writeTorsoMotion()
{
  if (state_->getNumberOfSupportLegs() == 0) state_->setEmptyControlSetup(BranchEnum::BASE);

  if (!queue_.getCurrentStep().hasBaseMotion()) return true;
  double time = queue_.getCurrentStep().getTime();
  // TODO Add frame handling.
  const auto& baseMotion = queue_.getCurrentStep().getBaseMotion();
  ControlSetup controlSetup = baseMotion.getControlSetup();
  state_->setControlSetup(BranchEnum::BASE, controlSetup);
  if (controlSetup[ControlLevel::Position]) {
    Pose pose = baseMotion.evaluatePose(time);
    state_->setPositionWorldToBaseInWorldFrame(pose.getPosition());
    state_->setOrientationWorldToBase(pose.getRotation());
  }
  if (controlSetup[ControlLevel::Velocity]) {
    Twist twist = baseMotion.evaluateTwist(time);
    state_->setLinearVelocityBaseInWorldFrame(twist.getTranslationalVelocity());
    state_->setAngularVelocityBaseInBaseFrame(twist.getRotationalVelocity());
  }
  // TODO Set more states.
  return true;
}

} /* namespace free_gait */
