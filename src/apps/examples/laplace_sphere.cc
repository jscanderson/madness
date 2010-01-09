/*
  This file is part of MADNESS.
  
  Copyright (C) 2007,2010 Oak Ridge National Laboratory
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
  
  For more information please contact:
  
  Robert J. Harrison
  Oak Ridge National Laboratory
  One Bethel Valley Road
  P.O. Box 2008, MS-6367
  
  email: harrisonrj@ornl.gov
  tel:   865-241-3937
  fax:   865-572-0680
  
  $Id$
*/

#define WORLD_INSTANTIATE_STATIC_TEMPLATES  

#include <mra/mra.h>
#include <constants.h>
#include <mra/sdf_shape_3D.h>
#include <linalg/solvers.h>
#include <examples/nonlinsol.h>
using namespace madness;

/*!
  \file examples/laplace_sphere.cc
  \brief Solves Laplace's equation on the interior and exterior of a sphere
  \defgroup laplace_sphere Use of interior boundary conditions to solve Laplace's equation
  \ingroup examples

  \par Points of interest
  - Use of interior boundary conditions
  - Use of KAIN solver
  - Line and surface plots
  - Use of Coulomb Green's function

  \par Background
  This example solves Laplace's equation on the interior and exterior of a sphere with
  Dirichlet boundary conditions.  Specifically, we solve 
  \f{eqnarray*}{
     \nabla^2 u(x) & = & 0 \\
     u(x) & = & \cos \theta \ \ |x|=1 
  \f}
  These simple boundary conditions are chosen to explore the accuracy
  of the solution since the exact solution is given by 
  \f{eqnarray*}{
  u(x) = \left\{  
           \begin{array}{l l}
             |x| \cos \theta & \quad |x| \le 1 \\
             |x|^{-2} \cos \theta & \quad \mbox{otherwise}
          \end{array}
        \right.
  \f}

  For potential problems there are several ways to proceed, but we
  follow the more generally applicable diffuse domain approach of
  X. Li, J. Lowengrub, A. R&auml;tz, and A. Voight, "Solving PDEs in
  Complex Geometries: A Diffuse Domain Approach," Commun. Math. Sci.,
  7, p81-107, 2009 using approximation 2 (equation 2.22 in the paper).
  The surface is represented using a diffuse layer (\f$ S(x) \f$) of
  width \f$ \epsilon \f$ computed here using the shape function
  library in mra/sdf_shape_3D.h .

  A penalty-like term is introduced into the equation
  \f[
     \nabla^2 u(x) - \epsilon^{-2} S(x) \left( u(x) - g(x) \right) = 0
  \f]
  where \f$ g(x) \f$ is the desired value of solution on the boundary
  (extended away from the boundary as a constant in the direction of
  the normal).

  Employing the known free-space Green's function (\f$ G(x) = -1/4 \pi |x| \f$) 
  yields the working equation and expression for the residual (\f$ r(x) \f$)
  \f[
      r = u - G * \left( \epsilon^{-2} S \left( u - g \right) \right) = 0
  \f]

  [It might be that approximation 3 is preferable ... needs testing.]

  \par Implementation

  The surface layer \f$ S(x) \f$ and boundary term \f$ S(x) g(x) \f$ (where
  \f$ g(x) = \cos \theta \f$) are computed.  Since we only have a functor
  available to compute the surface layer we must employ another functor
  to compose the product.  Note that the volume integral of the 
  surface layer should be normalized to the surface area of the sphere.

  A simpled fixed point iteration will not converge so it is necessary 
  to use a (non-)linear equation solver.  See examples/nonlinsol.h 
  for the one employed here.  Each iteration you provide the 
  current trial solution and the corresponding residual.  It returns
  the next trial solution vector --- note that for non-linear problems
  you probably have to employ step restriction (damping) or line search
  to get a stable solution.

*/

// A MADNESS functor combining two functors via multiplication
// Look in mra/testsuite.cc for a more general version (BinaryOp)
class Product : public FunctionFunctorInterface<double,3> {
    real_functor_3d left, right;

public:
    Product(const real_functor_3d& left, const real_functor_3d& right)
        : left(left), right(right)
    {}

    double operator()(const coord_3d& x) const {
        return (*left)(x) * (*right)(x);
    }
};

// Computes cos(theta) (easier to combine with phi if we 
// use a functor rather than a function)
class CosTheta : public FunctionFunctorInterface<double,3> {
public:
    double operator()(const coord_3d& x) const {
        double r = sqrt(x[0]*x[0] + x[1]*x[1] + x[2]*x[2]);
        return x[2]/r;
    }
};

// Computes the exact solution
class Exact :  public FunctionFunctorInterface<double,3> {
public:
    double operator()(const coord_3d& x) const {
        double r = sqrt(x[0]*x[0] + x[1]*x[1] + x[2]*x[2]);
        double c = x[2]/r;
        
        if (r < 1.0) return c*r;
        return c/(r*r);
    }
};

// Lowengrub's second approx for Dirichlet
void approx2(World& world, double epsilon, const coord_3d& center) {
    if (world.rank() == 0) print("\nStarting solution method 1\n");
    if (world.rank() == 0) print("Making S (normalized surface function)");
    real_functor_3d S_functor(shape_surface(epsilon, new SDFSphere(1.0, center)));
    real_function_3d S = real_factory_3d(world).functor(S_functor);
    double area = S.trace();
    if (world.rank() == 0) print("Surface area:", area, "error is", area-4*constants::pi);
    
    if (world.rank() == 0) print("Making S*g");
    real_functor_3d g_functor(new CosTheta);
    real_functor_3d Sg_functor(new Product(S_functor,g_functor));
    real_function_3d Sg = real_factory_3d(world).functor(Sg_functor);
    
    S *= 1.0/(epsilon*epsilon);
    Sg *= 1.0/(epsilon*epsilon);
    
    plotdx(S, "S.dx");
    plot_line("S.dat", 10001, coord_3d(-1.5), coord_3d(+1.5), S);
    
    // Make the Coulomb Green's function
    real_convolution_3d G = CoulombOperator<double>(world, FunctionDefaults<3>::get_k(), 
                                                    0.1*epsilon, FunctionDefaults<3>::get_thresh());
    // Initial guess for u is zero
    real_function_3d u = real_factory_3d(world);
    
    // Iterate
    NonlinearSolver solver;
    for (int iter=0; iter<7; iter++) {
        real_function_3d rhs = S*u - Sg;
        rhs.scale(-0.25/constants::pi);
        rhs.truncate();
        real_function_3d r = apply(G,rhs);
        r.truncate();
        r = r-u;
        
        real_function_3d unew = solver.update(u,r);
        
        double unorm=unew.norm2(), dunorm=(u-unew).norm2(), rnorm=r.norm2(), err=unew.err(Exact());
        if (world.rank() == 0) 
            print("iter", iter, "norm(u)", unorm, "norm(residual)", rnorm, "norm(u-unew)", dunorm, "norm(u-exact)", err);
        u = unew;
    }
    
    plotdx(u, "u.dx");
    plot_line("u.dat", 10001, coord_3d(-1.5), coord_3d(+1.5), u);
}

int main(int argc, char**argv) {
  initialize(argc,argv);
  World world(MPI::COMM_WORLD);
  startup(world,argc,argv);

  //FunctionDefaults<3>::set_truncate_on_project(true);
  FunctionDefaults<3>::set_cubic_cell(-3,3);
  FunctionDefaults<3>::set_thresh(1e-4);
  FunctionDefaults<3>::set_k(6);

  double epsilon = 0.1;   // surface width
  coord_3d center;        // (0,0,0)

  approx2(world, epsilon, center);

  finalize();

  return 0;
}
