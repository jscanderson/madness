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
#ifndef SVPE_MOLECULE_COLLOID_H
#define SVPE_MOLECULE_COLLOID_H

#include <mra/mra.h>
#include <mra/operator.h>
#include <mra/funcplot.h>
#include <linalg/solvers.h>
#include <examples/molecularmask.h>
#include <examples/nonlinsol.h>
#include <constants.h>
#include <vector>

using namespace madness;
using namespace std;

//set coordinates of colloid atoms                                                                                                                      
std::vector< madness::Vector<double,3> > colloid_coords(const double&d,const double R,const std::vector<double> cc) {
    std::vector< madness::Vector<double,3> > c(6); //6 spheres on the colloid surface                                                                   
    double sqrttwo = std::sqrt(2.0);
    double dist= (sqrttwo/2.0)*R;
    double x = cc[0], y =  cc[1], z = cc[2];                                                                                                  
    //double x = 0.0, y =  0.0, z = 0.0;
    c[0][0]=  x - dist, c[0][1]= y - d - dist, c[0][2] = z;
    //c[0][0]=  x, c[0][1]= y - d , c[0][2] = z;                                                                                                        
    c[1][0]= x + dist, c[1][1]= y - d - dist, c[1][2] = z;
    c[2][0]= x + dist, c[2][1]= y - d + dist, c[2][2] = z;
    c[3][0]= x - dist, c[3][1]= y - d + dist, c[3][2] = z;
    c[4][0]= x , c[4][1]=  y - d , c[4][2] = z + R;
    c[5][0]= x , c[5][1]= y - d, c[5][2] = z - R;
    //print("colloid coord",c);                                                                                                                         
    return c;
}

//colloid radii
//if number of colloid spheres changes don't forget to change it here
std::vector<double> colloid_radii(const double& R) {
    int nsphere = 6; //number of colloid spheres
    std::vector<double> c(nsphere);
    for(int i=0; i<nsphere; i++)
        c[i] = R;
    return c;
}

class SVPEColloidSolver {
    World& world;
    const double thresh;
    const double minlen;
    const double sigma;                 //< Width of surface layer
    const double epsilon_0;             //< Interior dielectric
    const double epsilon_1;             //< Exterior dielectric
    real_convolution_3d op;             //< Coulomb operator (1/r ... no 4pi)
    //std::vector<real_convolution_3d_ptr> gop; //< Gradient of the Coulomb operator
    vector_real_function_3d dlog; //< Log-derivative of the dielectric
    real_function_3d rdielectric; //< Reciprocal of the dielectric
    //this is used to perform a binary operation
    
    struct Bop {
        void operator()(const Key<3>& key,
                        real_tensor rfunc,
                        const real_tensor& func1,
                        const real_tensor& func2) const {
            ITERATOR(rfunc,
                     double d = func1(IND);
                     double p = func2(IND);
                     rfunc(IND) = d*p;
                     );
        }
        template <typename Archive>
        void serialize(Archive& ar) {}
    };
    real_function_3d func_pdt(const real_function_3d& func1,real_function_3d& func2) const {
        return binary_op(func1,func2, Bop());
    }
public:
    SVPEColloidSolver(World& world,
                      double sigma, double epsilon_0, double epsilon_1, 
                      const vector_real& atomic_radii, const vector_coord_3d& atomic_coords,
                      const double minlen)
        :world(world) 
        ,thresh(FunctionDefaults<3>::get_thresh())
        , minlen(minlen)
        , sigma(sigma)
        , epsilon_0(epsilon_0)
        , epsilon_1(epsilon_1)
        , op(CoulombOperator(world, minlen, thresh))
        , dlog(3)
    {
        // Functors for mask related quantities
        real_functor_3d rdielectric_functor(new MolecularVolumeExponentialSwitchReciprocal(sigma, epsilon_0, epsilon_1, atomic_radii, atomic_coords));
        real_functor_3d gradx_functor(new MolecularVolumeExponentialSwitchLogGrad(sigma, epsilon_0, epsilon_1, atomic_radii, atomic_coords,0));
        real_functor_3d grady_functor(new MolecularVolumeExponentialSwitchLogGrad(sigma, epsilon_0, epsilon_1, atomic_radii, atomic_coords,1));
        real_functor_3d gradz_functor(new MolecularVolumeExponentialSwitchLogGrad(sigma, epsilon_0, epsilon_1, atomic_radii, atomic_coords,2));
        
        // Make the actual functions
        const double rfourpi = 1.0/(4.0*constants::pi);
        rdielectric = real_factory_3d(world).functor(rdielectric_functor).nofence();
        dlog[0] = real_factory_3d(world).functor(gradx_functor).nofence();
        dlog[1] = real_factory_3d(world).functor(grady_functor).nofence();
        dlog[2] = real_factory_3d(world).functor(gradz_functor); // FENCE
        scale(world, dlog, rfourpi);
        rdielectric.truncate(false);
        truncate(world, dlog);
    }

    // Given the full Coulomb potential computes the surface charge
    real_function_3d make_surface_charge(const real_function_3d& u) const {
        real_derivative_3d Dx = free_space_derivative<double,3>(u.world(), 0);
        real_derivative_3d Dy = free_space_derivative<double,3>(u.world(), 1);
        real_derivative_3d Dz = free_space_derivative<double,3>(u.world(), 2);
        double fac = -1.0/(4.0*constants::pi);
        real_function_3d dx = Dx(u);
        real_function_3d dy = Dy(u);
        real_function_3d dz = Dz(u);
        real_function_3d sc = (func_pdt(dlog[0],dx) + func_pdt(dlog[1],dy) + func_pdt(dlog[2],dz)).truncate();
        return sc.scale(fac);
    }
    //computes components of the the electric field due to the surface charge 
    real_function_3d make_electric_field(const real_function_3d& u) const {
        real_derivative_3d Dx = free_space_derivative<double,3>(u.world(), 0);
        real_derivative_3d Dy = free_space_derivative<double,3>(u.world(), 1);
        real_derivative_3d Dz = free_space_derivative<double,3>(u.world(), 2);
        double fac = -1.0/(4.0*constants::pi);
        real_function_3d Sigma =(Dx(u) + Dy(u) + Dz(u)).scale(fac); //excess charge on colloid surface
        real_function_3d uxc  = op(Sigma); //coulomb potential due to excess charge
        real_function_3d dx = Dx(uxc) ;
        real_function_3d dy = Dy(uxc) ;
        real_function_3d dz = Dz(uxc) ;
        return (dx + dy + dz).scale(-1.0);
    }

    /// Solve for the full Coulomb potential using the free-particle GF
    real_function_3d solve(const real_function_3d& rho, 
                           const real_function_3d uguess = real_function_3d()) const {
        real_function_3d charge = rdielectric*rho;
        charge.truncate();
        
        // Initial guess is constant dielectric        
        real_function_3d u0 = op(charge).truncate();
        real_function_3d u = uguess.is_initialized() ? uguess : u0;
        double unorm = u.norm2();
        NonlinearSolver solver;
        for (int iter=0; iter<20; iter++) {
            double start = wall_time();
            real_function_3d surface_charge = make_surface_charge(u);
            real_function_3d r = (u - u0 + op(surface_charge)).truncate();
            double sigtot = surface_charge.trace();
            surface_charge.clear();
            
            real_function_3d unew = solver.update(u, r);
            
            double change = (unew-u).norm2();
            if (world.rank()==0){
                print("\n\n");//for formating output 
                madness::print("            Computing the Perturbed Potential Near              ");
                madness::print("                    the Colloid Surface                         ");
                madness::print("                   ______________________                    \n ");
                
                print("iter", iter, "change", change,
                      "soln(10.0)", u(coord_3d(10.0)),
                      "surface charge", sigtot,"used",wall_time()-start);
                print("\n\n");
            }
            // Step restriction 
            if (change > 0.3*unorm) 
                u = 0.5*unew + 0.5*u;
            else 
                u = unew;
            
            if (change < std::max(1e-3,10.0*thresh)) break;
        }
        return u - op(rho);
    }
    /// Solve for the full Coulomb potential using the free-particle GF
    real_function_3d solve_Laplace(const real_function_3d uguess) const {
        // Initial guess is constant dielectric        
        real_function_3d u = uguess;
        double unorm = u.norm2();
        NonlinearSolver solver;
        for (int iter=0; iter<20; iter++) {
            double start = wall_time();
            real_function_3d surface_charge = make_surface_charge(u);
            real_function_3d r = (u - op(surface_charge)).truncate();
            double sigtot = surface_charge.trace();
            surface_charge.clear();
            
            real_function_3d unew = solver.update(u, r);
            
            double change = (unew-u).norm2();
            if (world.rank()==0){
                print("\n\n");//for formating output 
                madness::print("            Computing the Perturbed Potential               ");
                madness::print("                   ______________________                   \n ");
                 
                print("iter", iter, "change", change,
                      "soln(10.0)", u(coord_3d(10.0)),
                      "surface charge", sigtot,"used",wall_time()-start);
            }
            // Step restriction 
            if (change > 0.3*unorm) 
                u = 0.5*unew + 0.5*u;
            else 
                u = unew;
            
            if (change < std::max(1e-3,10.0*thresh)) break;
        }
        return u;
    }
};

#endif
