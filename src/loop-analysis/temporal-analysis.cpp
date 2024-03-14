#include "loop-analysis/temporal-analysis.hpp"

#include <boost/range/adaptor/indexed.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include "isl-wrapper/isl-functions.hpp"

namespace analysis
{

/******************************************************************************
 * Local declarations
 *****************************************************************************/

std::pair<Occupancy, Fill> FillFromOccupancy(const Occupancy&, bool);

/******************************************************************************
 * Global function implementations
 *****************************************************************************/

TemporalReuseAnalysisInput::TemporalReuseAnalysisInput(
  const Occupancy& occupancy,
  BufTemporalReuseOpts reuse_opts
) : occupancy(occupancy), reuse_opts(reuse_opts)
{
}

TemporalReuseAnalysisOutput
TemporalReuseAnalysis(TemporalReuseAnalysisInput input)
{
  auto exploit_temporal_reuse = input.reuse_opts.exploit_temporal_reuse;
  const auto& occupancy = input.occupancy;
  bool multi_loop_reuse = input.reuse_opts.multi_loop_reuse;

  if (exploit_temporal_reuse){
    auto [eff_occ, fill] = FillFromOccupancy(occupancy, multi_loop_reuse);
    return TemporalReuseAnalysisOutput{
      .effective_occupancy = std::move(eff_occ),
      .fill = std::move(fill)
    };
  }
  else
  {
    return TemporalReuseAnalysisOutput{
      .effective_occupancy = occupancy,
      .fill = Fill(occupancy.dim_in_tags, occupancy.map)
    };
  }
}

/******************************************************************************
 * Local function implementations
 *****************************************************************************/

std::pair<Occupancy, Fill> FillFromOccupancy(const Occupancy& occupancy,
					     bool multi_loop_reuse)
{
  using namespace boost::adaptors;

  auto p_occ = occupancy.map.copy();
  auto tags = occupancy.dim_in_tags;
  for (const auto& it: occupancy.dim_in_tags | indexed(0) | reversed)
  {
    auto dim_tag = it.value();
    auto dim_idx = it.index();

    if (!(std::holds_alternative<Temporal>(dim_tag)
          || std::holds_alternative<Sequential>(dim_tag)
          || std::holds_alternative<PipelineTemporal>(dim_tag)
         ))
    {
      continue;
    }

    // Check if temporal dimension is "trivial," i.e., equals a singular value
    auto p_proj_occ =
      isl_map_project_out(isl_map_copy(p_occ), isl_dim_in, dim_idx, 1);
    auto p_reinserted_occ = isl_map_intersect_domain(
      isl_map_insert_dims(isl_map_copy(p_proj_occ), isl_dim_in, dim_idx, 1),
      isl_map_domain(isl_map_copy(p_occ))
    );

    if (isl_map_plain_is_equal(p_occ, p_reinserted_occ)
        || isl_map_is_equal(p_occ, p_reinserted_occ))
    {
      isl_map_free(p_reinserted_occ);
      isl_map_free(p_occ);
      p_occ = p_proj_occ;
      tags.erase(tags.begin() + dim_idx);
      continue;
    }

    isl_map_free(p_proj_occ);
    isl_map_free(p_reinserted_occ);

    isl_map* p_time_shift = nullptr;
    if (!multi_loop_reuse)
    {
      p_time_shift = isl::map_to_shifted(
        isl_space_domain(isl_map_get_space(p_occ)),
	dim_idx,
	-1
      );
    }
    else
    {
      isl_set* p_spacetime_domain = isl_map_domain(isl_map_copy(p_occ));
      isl_map* p_spacetime_domain_to_time_domain; // TODO
      isl_map* p_time_domain_to_past = isl_map_lex_gt(isl_set_get_space(isl_map_range(isl_map_copy(p_spacetime_domain_to_time_domain))));
      p_time_shift = isl_map_apply_range(
	isl_map_lexmax(p_time_domain_to_past),
	isl_map_reverse(p_spacetime_domain_to_time_domain)
      );
    }

    auto p_occ_before = isl_map_apply_range(p_time_shift,
					    isl_map_copy(p_occ));
    auto p_fill = isl_map_subtract(isl_map_copy(p_occ), p_occ_before);

    return std::make_pair(
      Occupancy(tags, isl::manage(p_occ)),
      Fill(tags, isl::manage(p_fill))
    );
  }

  return std::make_pair(Occupancy(tags, isl::manage(isl_map_copy(p_occ))),
                        Fill(tags, isl::manage(p_occ)));
}

} // namespace analysis
