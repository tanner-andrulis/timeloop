#include "isl-wrapper/isl-functions.hpp"

namespace isl {

isl::map
project_dim(isl::map map, isl_dim_type dim_type, size_t start, size_t n)
{
  return isl::manage(isl_map_project_out(map.release(), dim_type, start, n));
}

isl::map project_dim_in_after(isl::map map, size_t start)
{
  auto n_dim_in = isl_map_dim(map.get(), isl_dim_in);
  return project_dim(map, isl_dim_in, start, n_dim_in - start);
}

isl::map map_from_multi_aff(isl::multi_aff maff)
{
  return isl::manage(isl_map_from_multi_aff(maff.release()));
}
isl::map map_from_multi_aff(isl::pw_multi_aff maff)
{
  return isl::manage(isl_map_from_pw_multi_aff(maff.release()));
}

isl::space
space_alloc(isl::ctx ctx, size_t n_params, size_t n_dim_in, size_t n_dim_out)
{
  return isl::manage(isl_space_alloc(ctx.release(),
                                     n_params,
                                     n_dim_in,
                                     n_dim_out));
}

isl::aff
set_coefficient_si(isl::aff aff, isl_dim_type dim_type, size_t pos, int val)
{
  return isl::manage(isl_aff_set_coefficient_si(aff.release(),
                                                dim_type,
                                                pos,
                                                val));
}

isl::aff si_on_domain(isl::space space, int val)
{
  return isl::manage(isl_aff_val_on_domain_space(
    space.release(),
    isl_val_int_from_si(space.ctx().get(), val)
  ));
}

isl::map add_dims(isl::map map, isl_dim_type dim_type, size_t n_dims)
{
  return isl::manage(isl_map_add_dims(map.release(), dim_type, n_dims));
}

isl::map insert_dims(isl::map map,
                     isl_dim_type dim_type, size_t pos, size_t n_dims)
{
  return isl::manage(isl_map_insert_dims(map.release(),
                                         dim_type, pos, n_dims));
}

isl::map move_dims(isl::map map,
                   isl_dim_type dst_dim_type, size_t dst,
                   isl_dim_type src_dim_type, size_t src,
                   size_t n_dims)
{
  return isl::manage(
    isl_map_move_dims(map.release(),
                      dst_dim_type, dst,
                      src_dim_type, src,
                      n_dims)
  );
}

isl::map map_to_shifted(isl::space domain_space, size_t pos, int shift)
{
  auto p_maff = isl_multi_aff_identity_on_domain_space(domain_space.release());
  p_maff = isl_multi_aff_set_at(
    p_maff,
    pos,
    isl_aff_set_constant_si(isl_multi_aff_get_at(p_maff, pos), shift)
  );
  return isl::manage(isl_map_from_multi_aff(p_maff));
}

isl::map map_to_all_after(isl::space domain_space,
                          isl_dim_type dim_type, size_t pos)
{
  auto p_aff = isl_aff_zero_on_domain_space(domain_space.release());
  p_aff = isl_aff_set_coefficient_si(p_aff, dim_type, pos, 1);
  auto p_pw_aff = isl_pw_aff_from_aff(p_aff);
  return isl::manage(isl_pw_aff_lt_map(
    p_pw_aff,
    isl_pw_aff_copy(p_pw_aff)
  ));
}

isl::map fix_si(isl::map map, isl_dim_type dim_type, size_t pos, int val)
{
  return isl::manage(isl_map_fix_si(map.release(), dim_type, pos, val));
}

};  // namespace isl
