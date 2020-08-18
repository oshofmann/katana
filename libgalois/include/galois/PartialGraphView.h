#ifndef GALOIS_LIBGALOIS_GALOIS_PARTIALGRAPHVIEW_H_
#define GALOIS_LIBGALOIS_GALOIS_PARTIALGRAPHVIEW_H_

#include <memory>
#include <utility>

#include <boost/iterator/counting_iterator.hpp>

#include "galois/OutIndexView.h"
#include "galois/Range.h"
#include "galois/Result.h"
#include "tsuba/RDG.h"

namespace galois {

template <typename Edge>
class PartialGraphView {
  tsuba::RDG rdg_;
  galois::OutIndexView view_;

  const tsuba::GRPrefix* prefix_;
  const Edge* edges_;

  std::pair<uint64_t, uint64_t> node_range_;
  std::pair<uint64_t, uint64_t> edge_range_;

  PartialGraphView(tsuba::RDG&& rdg, galois::OutIndexView&& view,
                   std::pair<uint64_t, uint64_t> node_range,
                   std::pair<uint64_t, uint64_t> edge_range)
      : rdg_(std::move(rdg)), view_(std::move(view)), prefix_(view_.gr_view()),
        edges_(rdg_.topology_file_storage.valid_ptr<Edge>()),
        node_range_(std::move(node_range)), edge_range_(std::move(edge_range)) {
  }

  static uint64_t EdgeBegin(const tsuba::GRPrefix* prefix, uint64_t node_id) {
    if (node_id == 0) {
      return 0;
    }
    return prefix->out_indexes[node_id - 1];
  }

  static uint64_t EdgeEnd(const tsuba::GRPrefix* prefix, uint64_t node_id) {
    return prefix->out_indexes[node_id];
  }

public:
  typedef StandardRange<boost::counting_iterator<uint64_t>> edges_iterator;
  typedef StandardRange<boost::counting_iterator<uint64_t>> nodes_iterator;

  /// Make a partial graph view from a partially loaded RDG, as indicated by a
  /// RDGHandle and OutIndexView.
  static galois::Result<PartialGraphView>
  Make(tsuba::RDGHandle handle, OutIndexView&& oiv, uint64_t first_node,
       uint64_t last_node, const std::vector<std::string>& node_properties,
       const std::vector<std::string>& edge_properties) {

    auto view                     = std::move(oiv);
    const tsuba::GRPrefix* prefix = view.gr_view();

    uint64_t first_edge   = EdgeBegin(prefix, first_node);
    uint64_t last_edge    = EdgeBegin(prefix, last_node);
    uint64_t edges_offset = view.view_offset();
    uint64_t edges_start  = edges_offset + (first_edge * sizeof(Edge));
    uint64_t edges_stop   = edges_offset + (last_edge * sizeof(Edge));

    auto node_range = std::make_pair(first_node, last_node);
    auto edge_range = std::make_pair(first_edge, last_edge);

    auto rdg_res = tsuba::LoadPartial(handle, node_range, edge_range,
                                      edges_start, edges_stop - edges_start,
                                      node_properties, edge_properties);
    if (!rdg_res) {
      return rdg_res.error();
    }
    return PartialGraphView(std::move(rdg_res.value()), std::move(view),
                            node_range, edge_range);
  }

  nodes_iterator nodes() const {
    return MakeStandardRange(
        boost::counting_iterator<uint64_t>(node_range_.first),
        boost::counting_iterator<uint64_t>(node_range_.second));
  }

  edges_iterator all_edges() const {
    return MakeStandardRange(
        boost::counting_iterator<uint64_t>(edge_range_.first),
        boost::counting_iterator<uint64_t>(edge_range_.second));
  }

  edges_iterator edges(uint64_t node_id) const {
    return MakeStandardRange(
        boost::counting_iterator<uint64_t>(EdgeBegin(prefix_, node_id)),
        boost::counting_iterator<uint64_t>(EdgeEnd(prefix_, node_id)));
  }

  uint64_t GetEdgeDest(uint64_t edge_id) const {
    return edges_[edge_id - edge_range_.first];
  }

  const tsuba::RDG& prdg() const { return rdg_; }

  uint64_t node_offset() const { return node_range_.first; }
};

using PartialV1GraphView = PartialGraphView<uint32_t>;

} /* namespace galois */

#endif
