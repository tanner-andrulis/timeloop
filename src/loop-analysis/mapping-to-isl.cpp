/**
 * @file mapping-to-isl.cpp
 * @author Michael Gilbert (gilbertm@mit.edu)
 * @brief Implements conversion between mapping and analysis IR
 * @version 0.1
 * @date 2023-02-07
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#include "loop-analysis/isl-ir.hpp"

namespace analysis
{

/******************************************************************************
 * Local function declarations
 *****************************************************************************/

BranchTilings TilingFromMapping(const mapping::FusedMapping& mapping);
BranchTilings TilingFromMapping(const Mapping& mapping,
                                const problem::Workload& workload);


std::vector<std::pair<LogicalBuffer, size_t>> 
BufferIterLevelsFromMapping(const loop::Nest& nest,
                            const problem::Workload& workload);
std::vector<std::pair<LogicalBuffer, size_t>>
BufferIterLevelsFromMapping(const mapping::FusedMapping& mapping);

/**
 * @brief Utility to help TilingFromMapping track coefficients.
 */
struct TilingCoefTracker
{
  TilingCoefTracker();

  TilingCoefTracker&
  NewIterDim(const problem::Shape::FlattenedDimensionID& op_dim,
             const std::optional<size_t>& coef);

 private:
  friend IslMap TilingCoefTrackerToMap(TilingCoefTracker&& tracker);

  std::vector<std::vector<std::optional<size_t>>> coefs_;
};

IslMap TilingCoefTrackerToMap(TilingCoefTracker&& tracker);

LogicalBufTiling
LogicalBufTilingFromMapping(const mapping::FusedMapping& mapping);
LogicalBufTiling
LogicalBufTilingFromMapping(const loop::Nest& nest,
                            const problem::Workload& workload);

std::map<DataSpaceID, IslMap>
OpsToDSpaceFromEinsum(const problem::Workload& workload);

/******************************************************************************
 * Global function implementations
 *****************************************************************************/

LogicalBufOccupancies
OccupanciesFromMapping(const mapping::FusedMapping& mapping,
                       const problem::Workload& workload)
{
  auto ops_to_dspace = OpsToDSpaceFromEinsum(workload);
  auto buf_tiling = LogicalBufTilingFromMapping(mapping);

  LogicalBufOccupancies result;
  for (auto& [buf, tiling] : buf_tiling)
  {
    result.emplace(std::make_pair(
      buf,
      ApplyRange(std::move(tiling), IslMap(ops_to_dspace.at(buf.dspace_id)))
    ));
  }

  return result;
}

/******************************************************************************
 * Local function implementations
 *****************************************************************************/

BranchTilings
TilingFromMapping(const mapping::FusedMapping& mapping,
                  const problem::Workload& workload)
{
  BranchTilings result;
  for (const auto& path : GetPaths(mapping))
  {
    TilingCoefTracker coef_tracker;
    std::optional<IslPwMultiAff> explicit_tiling_spec;
    mapping::NodeID leaf_id;
    for (const auto& node : path)
    {
      std::visit(
        [&coef_tracker, &explicit_tiling_spec, &leaf_id] (auto&& node) {
          using NodeT = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<NodeT, mapping::For>
                        || std::is_same_v<NodeT, mapping::ParFor>)
          {
            coef_tracker.NewIterDim(node.op_dim, node.end);
          } else if constexpr (std::is_same_v<NodeT, mapping::Compute>)
          {
            explicit_tiling_spec = node.tiling_spec;
            leaf_id = node.id;
          }
        },
        node
      );
    }

    if (explicit_tiling_spec)
    {
      result.emplace(std::make_pair(
        leaf_id,
        IslMap::FromMultiAff(std::move(*explicit_tiling_spec))
      ));
    } else
    {
      result.emplace(std::make_pair(
        leaf_id,
        TilingCoefTrackerToMap(std::move(coef_tracker))
      ));
    }
  }

  return result;
}

std::vector<std::pair<LogicalBuffer, size_t>> 
BufferIterLevelsFromMapping(const loop::Nest& nest,
                            const problem::Workload& workload)
{
  std::vector<std::pair<LogicalBuffer, size_t>> result;

  std::set<decltype(nest.storage_tiling_boundaries)::value_type>
    tiling_boundaries(nest.storage_tiling_boundaries.begin(),
                      nest.storage_tiling_boundaries.end());

  // TODO: for now, buffer id in loop nest is the arch level
  BufferID arch_level = 0;
  auto loop_idx = 0;
  for (const auto& loop : nest.loops)
  {
    if (tiling_boundaries.find(loop_idx) != tiling_boundaries.end())
    {
      for (const auto& [dspace_id, _] : workload.GetShape()->DataSpaceIDToName)
      result.emplace_back(std::make_pair(
        LogicalBuffer{.buffer_id = arch_level,
                      .dspace_id = dspace_id,
                      .branch_leaf_id = 0 },
      loop_idx));
      ++arch_level;
    }
    ++ loop_idx;
  }

  return result;
}

std::vector<std::pair<LogicalBuffer, size_t>>
BufferIterLevelsFromMapping(const mapping::FusedMapping& mapping)
{
  std::vector<std::pair<LogicalBuffer, size_t>> result;
  for (const auto& path : GetPaths(mapping))
  {
    size_t iter_idx = 0;
    std::vector<std::pair<LogicalBuffer, size_t>> new_results;
    for (const auto& node : path)
    {
      std::visit(
        [&new_results, &iter_idx] (auto&& node) {
          using NodeT = std::decay_t<decltype(node)>;

          if constexpr (std::is_same_v<NodeT, mapping::Storage>)
          {
            auto buffer = LogicalBuffer();
            buffer.buffer_id = node.buffer;
            buffer.dspace_id = node.dspace;
            buffer.branch_leaf_id = 0;

            new_results.emplace_back(
              std::make_pair(std::move(buffer), iter_idx)
            );
          } else if constexpr (std::is_same_v<NodeT, mapping::For>
                               || std::is_same_v<NodeT, mapping::ParFor>)
          {
            ++iter_idx;
          } else if constexpr (std::is_same_v<NodeT, mapping::Compute>)
          {
            for (auto& [buf, _] : new_results)
            {
              buf.branch_leaf_id = node.id;
            }
          }
        },
        node
      );
    }
    result.insert(result.end(), new_results.begin(), new_results.end());
  }

  return result;
}

LogicalBufTiling
LogicalBufTilingFromMapping(const mapping::FusedMapping& mapping)
{
  auto branch_tiling = TilingFromMapping(mapping);
  auto buf_to_iter_level = BufferIterLevelsFromMapping(mapping);

  LogicalBufTiling result;
  for (auto& [buf, level] : buf_to_iter_level)
  {
    result.emplace(std::make_pair(
      buf,
      ProjectDimInAfter(IslMap(branch_tiling.at(buf.branch_leaf_id)),
                        level)
    ));
  }

  return result;
}

LogicalBufTiling
LogicalBufTilingFromMapping(const Mapping& mapping,
                            const problem::Workload& workload)
{
  const auto& nest = mapping.complete_loop_nest;
  auto branch_tiling = TilingFromMapping(mapping, workload);
  auto buf_to_iter_level = BufferIterLevelsFromMapping(nest, workload);

  LogicalBufTiling result;
  for (auto& [buf, level] : buf_to_iter_level)
  {
    result.emplace(std::make_pair(
      buf,
      ProjectDimInAfter(IslMap(branch_tiling.at(buf.branch_leaf_id)),
                        level)
    ));
  }

  return result;
}

std::map<DataSpaceID, IslMap>
OpsToDSpaceFromEinsum(const problem::Workload& workload)
{
  const auto& workload_shape = *workload.GetShape();

  std::map<DataSpaceID, IslMap> dspace_id_to_ospace_to_dspace;

  for (const auto& [name, dspace_id] : workload_shape.DataSpaceNameToID)
  {
    const auto dspace_order = workload_shape.DataSpaceOrder.at(dspace_id);
    const auto& projection = workload_shape.Projections.at(dspace_id);

    auto space = IslSpace::Alloc(gCtx,
                                 0,
                                 workload_shape.NumFactorizedDimensions,
                                 dspace_order);
    for (const auto& [ospace_dim_name, ospace_dim_id] :
         workload_shape.FactorizedDimensionNameToID)
    {
      space.SetDimName(isl_dim_in, ospace_dim_id, ospace_dim_name);
    }
    for (unsigned dspace_dim = 0; dspace_dim < dspace_order; ++dspace_dim)
    {
      const auto isl_dspace_dim_name = name + "_" + std::to_string(dspace_dim);
      space.SetDimName(isl_dim_out, dspace_dim, isl_dspace_dim_name);
    }

    auto multi_aff = IslMultiAff::Zero(IslSpace(space));
    for (unsigned dspace_dim = 0; dspace_dim < dspace_order; ++dspace_dim)
    {
      auto aff = IslAff::ZeroOnDomainSpace(IslSpaceDomain(IslSpace(space)));
      for (const auto& term : projection.at(dspace_dim))
      {
        const auto& coef_id = term.first;
        const auto& factorized_dim_id = term.second;
        if (coef_id != workload_shape.NumCoefficients)
        {
          aff.SetCoefficientSi(isl_dim_in,
                               factorized_dim_id,
                               workload.GetCoefficient(coef_id));
        }
        else // Last term is a constant
        {
          aff.SetCoefficientSi(isl_dim_in, factorized_dim_id, 1);
        }
        multi_aff.SetAff(dspace_dim, std::move(aff));
      }
    }
    dspace_id_to_ospace_to_dspace[dspace_id] =
        IslMap::FromMultiAff(std::move(multi_aff));
  }

  return dspace_id_to_ospace_to_dspace;
}

};