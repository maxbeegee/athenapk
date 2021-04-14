#ifndef HYDRO_HYDRO_HPP_
#define HYDRO_HYDRO_HPP_
//========================================================================================
// AthenaPK - a performance portable block structured AMR astrophysical MHD code.
// Copyright (c) 2020, Athena-Parthenon Collaboration. All rights reserved.
// Licensed under the BSD 3-Clause License (the "LICENSE").
//========================================================================================

// Parthenon headers
#include <parthenon/package.hpp>

#include "../eos/adiabatic_hydro.hpp"

using namespace parthenon::package::prelude;

namespace Hydro {

parthenon::Packages_t ProcessPackages(std::unique_ptr<ParameterInput> &pin);
std::shared_ptr<StateDescriptor> Initialize(ParameterInput *pin);

template <Fluid fluid>
Real EstimateTimestep(MeshData<Real> *md);

template <Fluid fluid, Reconstruction recon>
TaskStatus CalculateFluxes(std::shared_ptr<MeshData<Real>> &md);
using FluxFun_t = decltype(CalculateFluxes<Fluid::undefined, Reconstruction::undefined>);

TaskStatus AddUnsplitSources(MeshData<Real> *md, const Real beta_dt);
TaskStatus AddSplitSourcesFirstOrder(MeshData<Real> *md, const parthenon::SimTime &tm);

using SourceFirstOrderFun_t =
    std::function<void(MeshData<Real> *md, const parthenon::SimTime &tm)>;
static void ProblemSourceFirstOrderDefault(MeshData<Real> *md,
                                           const parthenon::SimTime &tm) {}
using SourceUnsplitFun_t = std::function<void(MeshData<Real> *md, const Real beta_dt)>;
static void ProblemSourceUnsplitDefault(MeshData<Real> *md, const Real beta_dt) {}

const char source_first_order_param_key[] = "ProblemSourceFirstOrder";
const char source_unsplit_param_key[] = "ProblemSourceUnsplit";

} // namespace Hydro

#endif // HYDRO_HYDRO_HPP_
