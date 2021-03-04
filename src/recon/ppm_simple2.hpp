//========================================================================================
// AthenaXXX astrophysical plasma code
// Copyright(C) 2020 James M. Stone <jmstone@ias.edu> and the Athena code team
// Licensed under the 3-clause BSD License (the "LICENSE")
//========================================================================================
//! \file ppm.cpp
//  \brief piecewise parabolic reconstruction with Collela-Sekora extremum preserving
//  limiters for a Cartesian-like coordinate with uniform spacing.
//
// This version does not include the extensions to the CS limiters described by
// McCorquodale et al. and as implemented in Athena++ by K. Felker.  This is to keep the
// code simple, because Kyle found these extensions did not improve the solution very
// much in practice, and because they can break monotonicity.
//
// REFERENCES:
// (CW) P. Colella & P. Woodward, "The Piecewise Parabolic Method (PPM) for Gas-Dynamical
// Simulations", JCP, 54, 174 (1984)
//
// (CS) P. Colella & M. Sekora, "A limiter for PPM that preserves accuracy at smooth
// extrema", JCP, 227, 7069 (2008)
//
// (MC) P. McCorquodale & P. Colella,  "A high-order finite-volume method for conservation
// laws on locally refined grids", CAMCoS, 6, 1 (2011)
//
// (PH) L. Peterson & G.W. Hammett, "Positivity preservation and advection algorithms
// with application to edge plasma turbulence", SIAM J. Sci. Com, 35, B576 (2013)

#include <algorithm> // max()
#include <math.h>

#include <parthenon/parthenon.hpp>

using parthenon::ScratchPad2D;

//----------------------------------------------------------------------------------------
//! \fn PPM()
//  \brief Reconstructs parabolic slope in cell i to compute ql(i+1) and qr(i). Works for
//  reconstruction in any dimension by passing in the appropriate q_im2,...,q _ip2.

KOKKOS_INLINE_FUNCTION
void PPM(const Real &q_im2, const Real &q_im1, const Real &q_i, const Real &q_ip1,
         const Real &q_ip2, Real &ql_ip1, Real &qr_i) {

  // CS08 constant used in second derivative limiter, >1 , independent of h
  const Real C2 = 1.25;
  const Real c1i = 0.5;
  const Real c2i = 0.5;
  const Real c3i = 0.5;
  const Real c4i = 0.5;
  const Real c5i = 1.0 / 6.0;
  const Real c6i = -1.0 / 6.0;
  //--- Step 1. --------------------------------------------------------------------------
  // Reconstruct interface averages <a>_{i-1/2} and <a>_{i+1/2}
  Real qa = (q_i - q_im1);
  Real qb = (q_ip1 - q_i);
  const Real dd_im1 = c1i * qa + c2i * (q_im1 - q_im2);
  const Real dd = c1i * qb + c2i * qa;
  const Real dd_ip1 = c1i * (q_ip2 - q_ip1) + c2i * qb;

  // Approximate interface average at i-1/2 and i+1/2 using PPM (CW eq 1.6)
  // KGF: group the biased stencil quantities to preserve FP symmetry
  Real dph = (c3i * q_im1 + c4i * q_i) + (c5i * dd_im1 + c6i * dd);
  Real dph_ip1 = (c3i * q_i + c4i * q_ip1) + (c5i * dd + c6i * dd_ip1);

  //--- Step 2a. -----------------------------------------------------------------------
  // Uniform Cartesian-like coordinate: limit interpolated interface states (CD 4.3.1)
  // approximate second derivative at interfaces for smooth extrema preservation
  // KGF: add the off-centered quantities first to preserve FP symmetry
  const Real d2qc_im1 = q_im2 + q_i - 2.0 * q_im1;
  const Real d2qc = q_im1 + q_ip1 - 2.0 * q_i; // (CD eq 85a) (no 1/2)
  const Real d2qc_ip1 = q_i + q_ip2 - 2.0 * q_ip1;

  // i-1/2
  Real qa_tmp = dph - q_im1; // (CD eq 84a)
  Real qb_tmp = q_i - dph;   // (CD eq 84b)
  // KGF: add the off-centered quantities first to preserve FP symmetry
  qa = 3.0 * (q_im1 + q_i - 2.0 * dph); // (CD eq 85b)
  qb = d2qc_im1;                        // (CD eq 85a) (no 1/2)
  Real qc = d2qc;                            // (CD eq 85c) (no 1/2)
  Real qd = 0.0;
  if (SIGN(qa) == SIGN(qb) && SIGN(qa) == SIGN(qc)) {
    qd =
        SIGN(qa) * std::min(C2 * std::abs(qb), std::min(C2 * std::abs(qc), std::abs(qa)));
  }
  Real dph_tmp = 0.5 * (q_im1 + q_i) - qd / 6.0;
  if (qa_tmp * qb_tmp < 0.0) { // Local extrema detected at i-1/2 face
    dph = dph_tmp;
  }

  // i+1/2
  qa_tmp = dph_ip1 - q_i;   // (CD eq 84a)
  qb_tmp = q_ip1 - dph_ip1; // (CD eq 84b)
  // KGF: add the off-centered quantities first to preserve FP symmetry
  qa = 3.0 * (q_i + q_ip1 - 2.0 * dph_ip1); // (CD eq 85b)
  qb = d2qc;                                // (CD eq 85a) (no 1/2)
  qc = d2qc_ip1;                            // (CD eq 85c) (no 1/2)
  qd = 0.0;
  if (SIGN(qa) == SIGN(qb) && SIGN(qa) == SIGN(qc)) {
    qd =
        SIGN(qa) * std::min(C2 * std::abs(qb), std::min(C2 * std::abs(qc), std::abs(qa)));
  }
  Real dphip1_tmp = 0.5 * (q_i + q_ip1) - qd / 6.0;
  if (qa_tmp * qb_tmp < 0.0) { // Local extrema detected at i+1/2 face
    dph_ip1 = dphip1_tmp;
  }

  // KGF: add the off-centered quantities first to preserve FP symmetry
  const Real d2qf = 6.0 * (dph + dph_ip1 - 2.0 * q_i); // a6 coefficient * -2

  // Cache Riemann states for both non-/uniform limiters
  Real qminus = dph;
  Real qplus = dph_ip1;

  //--- Step 3. ------------------------------------------------------------------------
  // Compute cell-centered difference stencils (MC section 2.4.1)
  const Real dqf_minus = q_i - qminus; // (CS eq 25) = -dQ^- in Mignone's notation
  const Real dqf_plus = qplus - q_i;

  //--- Step 4. ------------------------------------------------------------------------
  // For uniform Cartesian-like coordinate: apply CS limiters to parabolic interpolant
  qa_tmp = dqf_minus * dqf_plus;
  qb_tmp = (q_ip1 - q_i) * (q_i - q_im1);

  qa = d2qc_im1;
  qb = d2qc;
  qc = d2qc_ip1;
  qd = d2qf;
  Real qe = 0.0;
  if (SIGN(qa) == SIGN(qb) && SIGN(qa) == SIGN(qc) && SIGN(qa) == SIGN(qd)) {
    // Extrema is smooth
    qe = SIGN(qd) * std::min(std::min(C2 * std::abs(qa), C2 * std::abs(qb)),
                             std::min(C2 * std::abs(qc),
                                      std::abs(qd))); // (CS eq 22)
  }

  // Check if 2nd derivative is close to roundoff error
  qa = std::max(std::abs(q_im1), std::abs(q_im2));
  qb = std::max(std::max(std::abs(q_i), std::abs(q_ip1)), std::abs(q_ip2));

  Real rho = 0.0;
  if (std::abs(qd) > (1.0e-12) * std::max(qa, qb)) {
    // Limiter is not sensitive to roundoff. Use limited ratio (MC eq 27)
    rho = qe / qd;
  }

  Real tmp_m = q_i - rho * dqf_minus;
  Real tmp_p = q_i + rho * dqf_plus;
  Real tmp2_m = q_i - 2.0 * dqf_plus;
  Real tmp2_p = q_i + 2.0 * dqf_minus;

  // Check for local extrema
  if ((qa_tmp <= 0.0 || qb_tmp <= 0.0)) {
    // Check if relative change in limited 2nd deriv is > roundoff
    if (rho <= (1.0 - (1.0e-12))) {
      // Limit smooth extrema
      qminus = tmp_m; // (CS eq 23)
      qplus = tmp_p;
    }
    // No extrema detected
  } else {
    // Overshoot i-1/2,R / i,(-) state
    if (std::abs(dqf_minus) >= 2.0 * std::abs(dqf_plus)) {
      qminus = tmp2_m;
    }
    // Overshoot i+1/2,L / i,(+) state
    if (std::abs(dqf_plus) >= 2.0 * std::abs(dqf_minus)) {
      qplus = tmp2_p;
    }
  }

  //--- Step 5. ------------------------------------------------------------------------
  // Convert limited cell-centered values to interface-centered L/R Riemann states
  // both L/R values defined over [il,iu]
  // ql_iph = qplus;
  // qr_imh = qminus;

  // compute ql_(i+1/2) and qr_(i-1/2)
  // ql(n, i + 1) = ql_iph;
  // qr = qr_imh;
  ql_ip1 = qplus;
  qr_i = qminus;
}

//----------------------------------------------------------------------------------------
//! \fn PiecewiseParabolicX1()
//  \brief Wrapper function for PPM reconstruction in x1-direction.
//  This function should be called over [is-1,ie+1] to get BOTH L/R states over [is,ie]

template <typename T>
KOKKOS_INLINE_FUNCTION void
PiecewiseParabolicX1(parthenon::team_mbr_t const &member, const int k, const int j,
                     const int il, const int iu, const T &q, ScratchPad2D<Real> &ql,
                     ScratchPad2D<Real> &qr) {
  int nvar = q.GetDim(4);
  for (int n = 0; n < nvar; ++n) {
    parthenon::par_for_inner(member, il, iu, [&](const int i) {
      PPM(q(n, k, j, i - 2), q(n, k, j, i - 1), q(n, k, j, i), q(n, k, j, i + 1),
          q(n, k, j, i + 2), ql(n, i + 1), qr(n, i));
    });
  }
}

//----------------------------------------------------------------------------------------
//! \fn PiecewiseParabolicX2()
//  \brief Wrapper function for PPM reconstruction in x2-direction.
//  This function should be called over [js-1,je+1] to get BOTH L/R states over [js,je]

template <typename T>
KOKKOS_INLINE_FUNCTION void
PiecewiseParabolicX2(parthenon::team_mbr_t const &member, const int k, const int j,
                     const int il, const int iu, const T &q, ScratchPad2D<Real> &ql_jp1,
                     ScratchPad2D<Real> &qr_j) {
  int nvar = q.GetDim(4);
  for (int n = 0; n < nvar; ++n) {
    parthenon::par_for_inner(member, il, iu, [&](const int i) {
      PPM(q(n, k, j - 2, i), q(n, k, j - 1, i), q(n, k, j, i), q(n, k, j + 1, i),
          q(n, k, j + 2, i), ql_jp1(n, i), qr_j(n, i));
    });
  }
}

//----------------------------------------------------------------------------------------
//! \fn PiecewiseParabolicX3()
//  \brief Wrapper function for PPM reconstruction in x3-direction.
//  This function should be called over [ks-1,ke+1] to get BOTH L/R states over [ks,ke]

template <typename T>
KOKKOS_INLINE_FUNCTION void
PiecewiseParabolicX3(parthenon::team_mbr_t const &member, const int k, const int j,
                     const int il, const int iu, const T &q, ScratchPad2D<Real> &ql_kp1,
                     ScratchPad2D<Real> &qr_k) {
  int nvar = q.GetDim(4);
  for (int n = 0; n < nvar; ++n) {
    parthenon::par_for_inner(member, il, iu, [&](const int i) {
      PPM(q(n, k - 2, j, i), q(n, k - 1, j, i), q(n, k, j, i), q(n, k + 1, j, i),
          q(n, k + 2, j, i), ql_kp1(n, i), qr_k(n, i));
    });
  }
}
