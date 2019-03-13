#include "drake/systems/trajectory_optimization/multiple_shooting.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "drake/common/symbolic.h"
#include "drake/solvers/ipopt_solver.h"

using Eigen::MatrixXd;
using Eigen::VectorXd;

namespace drake {
namespace systems {
namespace trajectory_optimization {

using trajectories::PiecewisePolynomial;

// For readability of long lines, these single-letter variables names are
// sometimes used:
// N number of timesteps/samples
// h timesteps (there are N-1 of these)
// x state
// u control input

MultipleShooting::MultipleShooting(int num_inputs, int num_states,
                                   int num_time_samples, double fixed_timestep)
    : num_inputs_(num_inputs),
      num_states_(num_states),
      N_(num_time_samples),
      timesteps_are_decision_variables_(false),
      fixed_timestep_(fixed_timestep),
      h_vars_(solvers::VectorXDecisionVariable(0)),
      placeholder_t_var_(
          solvers::VectorDecisionVariable<1>(symbolic::Variable("t"))) {
  DRAKE_DEMAND(num_time_samples > 1);
  DRAKE_DEMAND(num_states_ > 0);
  DRAKE_DEMAND(num_inputs_ >= 0);
  DRAKE_DEMAND(fixed_timestep > 0);
  placeholder_x_vars_ = NewSequentialVariables(num_states_, "x");
  placeholder_u_vars_ = NewSequentialVariables(num_inputs_, "u");
  x_vars_ = solvers::VectorXDecisionVariable(N_ * num_states);
  u_vars_ = solvers::VectorXDecisionVariable(N_ * num_inputs);
  for (int i = 0; i < N_; ++i) {
    x_vars_.segment(num_states_ * i, num_states_) =
        GetSequentialVariablesAtIndex(placeholder_x_vars_, i);
    u_vars_.segment(num_inputs_ * i, num_inputs_) =
        GetSequentialVariablesAtIndex(placeholder_u_vars_, i);
  }
}

MultipleShooting::MultipleShooting(int num_inputs, int num_states,
                                   int num_time_samples,
                                   double minimum_timestep,
                                   double maximum_timestep)
    : num_inputs_(num_inputs),
      num_states_(num_states),
      N_(num_time_samples),
      timesteps_are_decision_variables_(true),
      h_vars_(NewContinuousVariables(N_ - 1, "h")),
      placeholder_t_var_(
          solvers::VectorDecisionVariable<1>(symbolic::Variable("t"))) {
  DRAKE_DEMAND(num_time_samples > 1);
  DRAKE_DEMAND(num_states_ > 0);
  DRAKE_DEMAND(num_inputs_ >= 0);
  DRAKE_DEMAND(minimum_timestep > 0);  // == 0 tends to cause numerical issues.
  DRAKE_DEMAND(maximum_timestep >= minimum_timestep &&
               std::isfinite(maximum_timestep));
  placeholder_x_vars_ = NewSequentialVariables(num_states_, "x");
  placeholder_u_vars_ = NewSequentialVariables(num_inputs_, "u");
  x_vars_ = solvers::VectorXDecisionVariable(N_ * num_states);
  u_vars_ = solvers::VectorXDecisionVariable(N_ * num_inputs);
  for (int i = 0; i < N_; ++i) {
    x_vars_.segment(num_states_ * i, num_states_) =
        GetSequentialVariablesAtIndex(placeholder_x_vars_, i);
    u_vars_.segment(num_inputs_ * i, num_inputs_) =
        GetSequentialVariablesAtIndex(placeholder_u_vars_, i);
  }

  AddBoundingBoxConstraint(minimum_timestep, maximum_timestep, h_vars_);
}

void MultipleShooting::AddTimeIntervalBounds(double lower_bound,
                                             double upper_bound) {
  DRAKE_THROW_UNLESS(timesteps_are_decision_variables_);
  AddBoundingBoxConstraint(lower_bound, upper_bound, h_vars_);
}

void MultipleShooting::AddEqualTimeIntervalsConstraints() {
  DRAKE_THROW_UNLESS(timesteps_are_decision_variables_);
  for (int i = 1; i < N_ - 1; i++) {
    AddLinearConstraint(h_vars_(i - 1) == h_vars_(i));
  }
}

void MultipleShooting::AddDurationBounds(double lower_bound,
                                         double upper_bound) {
  DRAKE_THROW_UNLESS(timesteps_are_decision_variables_);
  AddLinearConstraint(VectorXd::Ones(h_vars_.size()), lower_bound, upper_bound,
                      h_vars_);
}

solvers::Binding<solvers::VisualizationCallback>
MultipleShooting::AddInputTrajectoryCallback(
    const MultipleShooting::TrajectoryCallback& callback) {
  return AddVisualizationCallback(
      [this, callback](const Eigen::Ref<const Eigen::VectorXd>& x) {
        const Eigen::VectorXd times = GetSampleTimes(x.head(h_vars_.size()));
        const Eigen::Map<const Eigen::MatrixXd> inputs(
            x.data() + h_vars_.size(), num_inputs_, N_);
        callback(times, inputs);
      },
      {h_vars_, u_vars_});
}

solvers::Binding<solvers::VisualizationCallback>
MultipleShooting::AddStateTrajectoryCallback(
    const MultipleShooting::TrajectoryCallback& callback) {
  return AddVisualizationCallback(
      [this, callback](const Eigen::Ref<const Eigen::VectorXd>& x) {
        const Eigen::VectorXd times = GetSampleTimes(x.head(h_vars_.size()));
        const Eigen::Map<const Eigen::MatrixXd> states(
            x.data() + h_vars_.size(), num_states_, N_);
        callback(times, states);
      },
      {h_vars_, x_vars_});
}

void MultipleShooting::SetInitialTrajectory(
    const PiecewisePolynomial<double>& traj_init_u,
    const PiecewisePolynomial<double>& traj_init_x) {
  double start_time = 0;
  double h = fixed_timestep_;
  if (timesteps_are_decision_variables_) {
    double end_time = fixed_timestep_ * N_;
    DRAKE_THROW_UNLESS(!traj_init_u.empty() || !traj_init_x.empty());
    if (!traj_init_u.empty()) {
      start_time = traj_init_u.start_time();
      end_time = traj_init_u.end_time();
      if (!traj_init_x.empty()) {
        // Note: Consider adding a tolerance here if warranted.
        DRAKE_THROW_UNLESS(start_time == traj_init_x.start_time());
        DRAKE_THROW_UNLESS(end_time == traj_init_x.end_time());
      }
    } else {
      start_time = traj_init_x.start_time();
      end_time = traj_init_x.end_time();
    }
    DRAKE_DEMAND(start_time <= end_time);
    h = (end_time - start_time) / (N_ - 1);
    SetInitialGuess(h_vars_, VectorXd::Constant(h_vars_.size(), h));
  }

  VectorXd guess_u(u_vars_.size());
  if (traj_init_u.empty()) {
    guess_u.fill(0.003);  // Start with some small number <= 0.01.
  } else {
    for (int i = 0; i < N_; ++i) {
      guess_u.segment(num_inputs_ * i, num_inputs_) =
          traj_init_u.value(start_time + i * h);
    }
  }
  SetInitialGuess(u_vars_, guess_u);

  VectorXd guess_x(x_vars_.size());
  if (traj_init_x.empty()) {
    guess_x.fill(0.003);  // Start with some small number <= 0.01.
    // TODO(Lucy-tri) Do what DirectTrajectoryOptimization.m does.
  } else {
    for (int i = 0; i < N_; ++i) {
      guess_x.segment(num_states_ * i, num_states_) =
          traj_init_x.value(start_time + i * h);
    }
  }
  SetInitialGuess(x_vars_, guess_x);
}

Eigen::VectorXd MultipleShooting::GetSampleTimes(
    const Eigen::Ref<const Eigen::VectorXd>& h_var_values) const {
  Eigen::VectorXd times(N_);

  if (timesteps_are_decision_variables_) {
    times[0] = 0.0;
    for (int i = 1; i < N_; i++) {
      times[i] = times[i - 1] + h_var_values(i - 1);
    }
  } else {
    for (int i = 0; i < N_; i++) {
      times[i] = i * fixed_timestep_;
    }
  }
  return times;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
Eigen::MatrixXd MultipleShooting::GetInputSamples() const {
  Eigen::MatrixXd inputs(num_inputs_, N_);
  for (int i = 0; i < N_; i++) {
    inputs.col(i) = GetSolution(input(i));
  }
  return inputs;
}
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
Eigen::MatrixXd MultipleShooting::GetStateSamples() const {
  Eigen::MatrixXd states(num_states_, N_);
  for (int i = 0; i < N_; i++) {
    states.col(i) = GetSolution(state(i));
  }
  return states;
}
#pragma GCC diagnostic pop

Eigen::MatrixXd MultipleShooting::GetInputSamples(
    const solvers::MathematicalProgramResult& result) const {
  Eigen::MatrixXd inputs(num_inputs_, N_);
  for (int i = 0; i < N_; i++) {
    inputs.col(i) = result.GetSolution(input(i));
  }
  return inputs;
}

Eigen::MatrixXd MultipleShooting::GetStateSamples(
    const solvers::MathematicalProgramResult& result) const {
  Eigen::MatrixXd states(num_states_, N_);
  for (int i = 0; i < N_; i++) {
    states.col(i) = result.GetSolution(state(i));
  }
  return states;
}

symbolic::Substitution
MultipleShooting::ConstructPlaceholderVariableSubstitution(
    int interval_index) const {
  symbolic::Substitution sub;

  if (timesteps_are_decision_variables_) {
    // time(i) is the sum of h intervals 0...(i-1)
    const symbolic::Expression time =
        h_vars_.head(interval_index).cast<symbolic::Expression>().sum();
    sub.emplace(placeholder_t_var_(0), time);
  } else {
    sub.emplace(placeholder_t_var_(0), interval_index * fixed_timestep_);
  }

  AddPlaceholderVariableSubstitutionsForIndex(interval_index, &sub);

  return sub;
}

symbolic::Expression MultipleShooting::SubstitutePlaceholderVariables(
    const symbolic::Expression& e, int interval_index) const {
  return e.Substitute(ConstructPlaceholderVariableSubstitution(interval_index));
}

symbolic::Formula MultipleShooting::SubstitutePlaceholderVariables(
    const symbolic::Formula& f, int interval_index) const {
  return f.Substitute(ConstructPlaceholderVariableSubstitution(interval_index));
}

int MultipleShooting::SamplesPerSequentialVariable() const { return N_; }

}  // namespace trajectory_optimization
}  // namespace systems
}  // namespace drake
