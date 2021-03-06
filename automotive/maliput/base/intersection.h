#pragma once

#include <vector>

#include "drake/automotive/maliput/api/intersection.h"
#include "drake/automotive/maliput/api/rules/regions.h"
#include "drake/automotive/maliput/api/rules/right_of_way_phase_provider.h"
#include "drake/automotive/maliput/api/rules/right_of_way_phase_ring.h"
#include "drake/automotive/maliput/base/simple_right_of_way_phase_provider.h"
#include "drake/common/drake_copyable.h"
#include "drake/common/drake_optional.h"

namespace drake {
namespace maliput {

/// A convenience data structure for aggregating information about an
/// intersection. Its primary purpose is to serve as a single source of this
/// information and to remove the need to query numerous disparate data
/// structures and state providers.
class Intersection : public api::Intersection {
 public:
  DRAKE_NO_COPY_NO_MOVE_NO_ASSIGN(Intersection)

  /// Constructs an Intersection instance.
  ///
  /// @param id The intersection's unique ID.
  ///
  /// @param region The region of the road network that is part of the
  /// intersection.
  ///
  /// @param ring_id The ID of the ring that defines the phases within the
  /// intersection.
  ///
  /// @param phase_provider Enables the current phase within @p ring to be
  /// specified and obtained. The pointer must remain valid throughout this
  /// class instance's lifetime.
  Intersection(const Id& id, const std::vector<api::rules::LaneSRange>& region,
               const api::rules::RightOfWayPhaseRing::Id& ring_id,
               SimpleRightOfWayPhaseProvider* phase_provider);

  virtual ~Intersection() = default;

  /// Returns the current phase.
  const optional<api::rules::RightOfWayPhaseProvider::Result> Phase() const;

  /// Sets the current phase.
  void SetPhase(const api::rules::RightOfWayPhase::Id& phase_id);

  // TODO(liang.fok) Add method for obtaining the current bulb states

  // TODO(liang.fok) Add method for obtaining the intersection's bounding box.
 private:
  SimpleRightOfWayPhaseProvider* phase_provider_{};
};

}  // namespace maliput
}  // namespace drake
