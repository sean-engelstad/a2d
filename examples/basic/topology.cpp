#include <iostream>
#include <memory>
#include <random>

#include "multiphysics/elasticity.h"
#include "multiphysics/febasis.h"
#include "multiphysics/feelement.h"
#include "multiphysics/femesh.h"
#include "multiphysics/fequadrature.h"
#include "multiphysics/heat_conduction.h"
#include "multiphysics/hex_tools.h"
#include "multiphysics/lagrange_hex_basis.h"
#include "multiphysics/poisson.h"
#include "multiphysics/qhdiv_hex_basis.h"
#include "sparse/sparse_amg.h"

template <A2D::index_t degree>
class TopoOpt {
 public:
  static const A2D::index_t dim = 3;
  static const A2D::index_t data_dim = 1;
  using T = double;
  using PDE = A2D::TopoLinearElasticity<T, dim>;

  using Quadrature = A2D::HexGaussQuadrature<degree + 1>;
  using DataBasis =
      A2D::FEBasis<T, A2D::LagrangeL2HexBasis<T, data_dim, degree - 1>>;
  using GeoBasis = A2D::FEBasis<T, A2D::LagrangeH1HexBasis<T, dim, degree>>;
  using Basis = A2D::FEBasis<T, A2D::LagrangeH1HexBasis<T, dim, degree>>;
  using DataElemVec = A2D::ElementVector_Serial<T, DataBasis>;
  using GeoElemVec = A2D::ElementVector_Serial<T, GeoBasis>;
  using ElemVec = A2D::ElementVector_Serial<T, Basis>;
  using FE = A2D::FiniteElement<T, PDE, Quadrature, DataBasis, GeoBasis, Basis>;

  // Matrix-free operator for the problem
  using MatFree =
      A2D::MatrixFree<T, PDE, Quadrature, DataBasis, GeoBasis, Basis>;

  static const A2D::index_t low_degree = 1;
  using LOrderQuadrature = A2D::HexGaussQuadrature<low_degree + 1>;
  using LOrderDataBasis =
      A2D::FEBasis<T, A2D::LagrangeL2HexBasis<T, data_dim, low_degree - 1>>;
  using LOrderGeoBasis =
      A2D::FEBasis<T, A2D::LagrangeH1HexBasis<T, dim, low_degree>>;
  using LOrderBasis =
      A2D::FEBasis<T, A2D::LagrangeH1HexBasis<T, dim, low_degree>>;
  using LOrderDataElemVec = A2D::ElementVector_Serial<T, LOrderDataBasis>;
  using LOrderGeoElemVec = A2D::ElementVector_Serial<T, LOrderGeoBasis>;
  using LOrderElemVec = A2D::ElementVector_Serial<T, LOrderBasis>;
  using LOrderFE = A2D::FiniteElement<T, PDE, LOrderQuadrature, LOrderDataBasis,
                                      LOrderGeoBasis, LOrderBasis>;

  // Block-size for the finite-element problem
  static const A2D::index_t block_size = 3;
  static const A2D::index_t null_size = 6;
  using BSRMatType = A2D::BSRMat<A2D::index_t, T, block_size, block_size>;
  using BSRMatAmgType = A2D::BSRMatAmg<A2D::index_t, T, block_size, null_size>;

  TopoOpt(A2D::MeshConnectivity3D &conn, A2D::DirichletBCInfo &bcinfo, T E,
          T nu, T q)
      :  // Meshes for the solution, geometry and data
        mesh(conn),
        geomesh(conn),
        datamesh(conn),

        bcs(conn, mesh, bcinfo),

        // Project the meshes onto the low-order meshes
        lorder_mesh(mesh, basis_proj),
        lorder_geomesh(geomesh, geo_proj),
        lorder_datamesh(datamesh, data_proj),

        // Solution, geometry and data vectors
        sol(mesh.get_num_dof()),
        geo(geomesh.get_num_dof()),
        data(datamesh.get_num_dof()),

        // Element-level views of the solution geometry and data
        elem_data(datamesh, data),
        elem_geo(geomesh, geo),
        elem_sol(mesh, sol),

        // Low-order views of the solution geometry and data
        lorder_elem_data(lorder_datamesh, data),
        lorder_elem_geo(lorder_geomesh, geo),
        lorder_elem_sol(lorder_mesh, sol),

        pde(E, nu, q),
        B("B", sol.get_num_dof() / block_size) {
    // Initialize the data
    for (A2D::index_t i = 0; i < data.get_num_dof(); i++) {
      data[i] = 1.0;
    }

    // Create the matrix for the low-order mesh
    A2D::index_t nrows;
    std::vector<A2D::index_t> rowp, cols;
    lorder_mesh.create_block_csr<block_size>(nrows, rowp, cols);

    // Create the shared pointer
    mat = std::make_shared<BSRMatType>(nrows, nrows, cols.size(), rowp, cols);
  }

  GeoElemVec &get_geometry() { return elem_geo; }

  void reset_geometry() {
    A2D::SolutionVector<T> x(mesh.get_num_dof());
    A2D::SolutionVector<T> y(mesh.get_num_dof());
    A2D::SolutionVector<T> z(mesh.get_num_dof());

    ElemVec elem_x(mesh, x);
    ElemVec elem_y(mesh, y);
    ElemVec elem_z(mesh, z);

    A2D::DOFCoordinates<T, PDE, GeoBasis, Basis> coords;
    coords.get_dof_coordinates(elem_geo, elem_x, elem_y, elem_z);

    // Initialize the near null-space to an appropriate vector
    for (A2D::index_t i = 0; i < B.extent(0); i++) {
      B(i, 0, 0) = 1.0;
      B(i, 1, 1) = 1.0;
      B(i, 2, 2) = 1.0;

      // Rotation about the x-axis
      B(i, 1, 3) = z[3 * i + 2];
      B(i, 2, 3) = -y[3 * i + 1];

      // Rotation about the y-axis
      B(i, 0, 4) = z[3 * i + 2];
      B(i, 2, 4) = -x[3 * i];

      // Rotation about the z-axis
      B(i, 0, 5) = y[3 * i + 1];
      B(i, 1, 5) = -x[3 * i];
    }

    // Zero out the boundary conditions
    const A2D::index_t *bc_dofs;
    A2D::index_t nbcs = bcs.get_bcs(&bc_dofs);
    for (A2D::index_t i = 0; i < nbcs; i++) {
      A2D::index_t dof = bc_dofs[i];
      for (A2D::index_t j = 0; j < null_size; j++) {
        B(dof / block_size, dof % block_size, j) = 0.0;
      }
    }
  }

  /**
   * @brief Solve the governing equations and set the new solution vector
   *
   */
  void solve() {
    // Create a view of the low-order element matrix
    A2D::ElementMat_Serial<T, LOrderBasis, BSRMatType> elem_mat(lorder_mesh,
                                                                *mat);

    // Initialie the Jacobian matrix
    lorder_fe.add_jacobian(pde, lorder_elem_data, lorder_elem_geo,
                           lorder_elem_sol, elem_mat);

    // Apply the boundary conditions
    const A2D::index_t *bc_dofs;
    A2D::index_t nbcs = bcs.get_bcs(&bc_dofs);
    mat->zero_rows(nbcs, bc_dofs);

    // Initialize the matrix-free data
    matfree.initialize(pde, elem_data, elem_geo, elem_sol);

    // Allocate space for temporary variables with the matrix-vector code
    A2D::SolutionVector<T> xvec(mesh.get_num_dof());
    A2D::SolutionVector<T> yvec(mesh.get_num_dof());
    ElemVec elem_xvec(mesh, xvec);
    ElemVec elem_yvec(mesh, yvec);

    auto mat_vec = [&](A2D::MultiArrayNew<T *[block_size]> &in,
                       A2D::MultiArrayNew<T *[block_size]> &out) -> void {
      xvec.zero();
      yvec.zero();
      for (A2D::index_t i = 0; i < xvec.get_num_dof(); i++) {
        xvec[i] = in(i / block_size, i % block_size);
      }
      matfree.add_jacobian_vector_product(elem_xvec, elem_yvec);

      for (A2D::index_t i = 0; i < yvec.get_num_dof(); i++) {
        out(i / block_size, i % block_size) = yvec[i];
      }

      // Set the boundary conditions as equal to the inputs
      const A2D::index_t *bc_dofs;
      A2D::index_t nbcs = bcs.get_bcs(&bc_dofs);
      for (A2D::index_t i = 0; i < nbcs; i++) {
        A2D::index_t dof = bc_dofs[i];

        out(dof / block_size, dof % block_size) =
            in(dof / block_size, dof % block_size);
      }
    };

    // Allocate the solver - we should add some of these as solver options
    A2D::index_t num_levels = 3;
    double omega = 3.0 / 4.0;
    double epsilon = 0.0;
    bool print_info = true;
    BSRMatAmgType amg(num_levels, omega, epsilon, mat, B, print_info);

    // Create the solution and right-hand-side vectors
    A2D::index_t size = sol.get_num_dof() / block_size;
    A2D::MultiArrayNew<T *[block_size]> sol_vec("sol_vec", size);
    A2D::MultiArrayNew<T *[block_size]> force_vec("force_vec", size);

    // Set a constant right-hand-side
    for (A2D::index_t i = 0; i < force_vec.extent(0); i++) {
      for (A2D::index_t j = 0; j < force_vec.extent(1); j++) {
        force_vec(i, j) = 1.0;
      }
    }

    // Zero out the boundary conditions
    for (A2D::index_t i = 0; i < nbcs; i++) {
      A2D::index_t dof = bc_dofs[i];
      force_vec(dof / block_size, dof % block_size) = 0.0;
    }

    // Solve the problem
    amg.cg(mat_vec, force_vec, sol_vec, 5, 50);

    // Record the solution
    for (A2D::index_t i = 0; i < sol.get_num_dof(); i++) {
      sol[i] = sol_vec(i / block_size, i % block_size);
    }
  }

  void tovtk(const char *filename) {
    A2D::write_hex_to_vtk<3, degree, T, DataBasis, GeoBasis, Basis>(
        pde, elem_data, elem_geo, elem_sol,
        [](A2D::index_t k, typename PDE::DataSpace &d,
           typename PDE::FiniteElementGeometry &g,
           typename PDE::FiniteElementSpace &s) {
          auto u = (s.template get<0>()).get_value();
          return u(k);
        });
  }

 private:
  A2D::ElementMesh<Basis> mesh;
  A2D::ElementMesh<GeoBasis> geomesh;
  A2D::ElementMesh<DataBasis> datamesh;

  A2D::DirichletBCs<Basis> bcs;

  A2D::HexProjection<degree, Basis, LOrderBasis> basis_proj;
  A2D::HexProjection<degree, GeoBasis, LOrderGeoBasis> geo_proj;
  A2D::HexProjection<degree, DataBasis, LOrderDataBasis> data_proj;

  A2D::ElementMesh<LOrderBasis> lorder_mesh;
  A2D::ElementMesh<LOrderGeoBasis> lorder_geomesh;
  A2D::ElementMesh<LOrderDataBasis> lorder_datamesh;

  A2D::SolutionVector<T> sol;
  A2D::SolutionVector<T> geo;
  A2D::SolutionVector<T> data;

  DataElemVec elem_data;
  GeoElemVec elem_geo;
  ElemVec elem_sol;

  LOrderDataElemVec lorder_elem_data;
  LOrderGeoElemVec lorder_elem_geo;
  LOrderElemVec lorder_elem_sol;

  PDE pde;
  FE fe;
  LOrderFE lorder_fe;
  MatFree matfree;

  // The near null-space to an appropriate vector
  A2D::MultiArrayNew<T *[block_size][null_size]> B;

  // System matrix
  std::shared_ptr<BSRMatType> mat;
};

int main(int argc, char *argv[]) {
  Kokkos::initialize();

  // test_febasis();

  std::cout << "Topology linear elasticity\n";
  A2D::TopoLinearElasticity<std::complex<double>, 3> elasticity(70e3, 0.3, 5.0);
  A2D::TestPDEImplementation<std::complex<double>>(elasticity);

  // Number of elements in each dimension
  const int nx = 8, ny = 4, nz = 4;
  auto node_num = [](int i, int j, int k) {
    return i + j * (nx + 1) + k * (nx + 1) * (ny + 1);
  };

  // Number of edges
  const int nverts = (nx + 1) * (ny + 1) * (nz + 1);
  int ntets = 0, nwedge = 0, npyrmd = 0;
  const int nhex = nx * ny * nz;

  int *tets = NULL, *wedge = NULL, *pyrmd = NULL;
  int hex[8 * nhex];

  using ET = A2D::ElementTypes;

  for (int k = 0, e = 0; k < nz; k++) {
    for (int j = 0; j < ny; j++) {
      for (int i = 0; i < nx; i++, e++) {
        for (int ii = 0; ii < ET::HEX_VERTS; ii++) {
          hex[8 * e + ii] = node_num(i + ET::HEX_VERTS_CART[ii][0],
                                     j + ET::HEX_VERTS_CART[ii][1],
                                     k + ET::HEX_VERTS_CART[ii][2]);
        }
      }
    }
  }

  double Xloc[3 * nverts];
  for (int k = 0; k < nz + 1; k++) {
    for (int j = 0; j < ny + 1; j++) {
      for (int i = 0; i < nx + 1; i++) {
        Xloc[3 * node_num(i, j, k)] = (2.0 * i) / nx;
        Xloc[3 * node_num(i, j, k) + 1] = (1.0 * j) / ny;
        Xloc[3 * node_num(i, j, k) + 2] = (1.0 * k) / nz;
      }
    }
  }

  // Constrain the nodes at either end of the block
  const int num_boundary_verts = 2 * (ny + 1) * (nz + 1);
  int boundary_verts[num_boundary_verts];
  for (int k = 0, index = 0; k < nz + 1; k++) {
    for (int j = 0; j < ny + 1; j++, index++) {
      boundary_verts[index] = node_num(0, j, k);
    }
  }

  for (int k = 0, index = (ny + 1) * (nz + 1); k < nz + 1; k++) {
    for (int j = 0; j < ny + 1; j++, index++) {
      boundary_verts[index] = node_num(nx, j, k);
    }
  }

  A2D::MeshConnectivity3D conn(nverts, ntets, tets, nhex, hex, nwedge, wedge,
                               npyrmd, pyrmd);

  A2D::index_t end_label =
      conn.add_boundary_label_from_verts(num_boundary_verts, boundary_verts);

  A2D::index_t basis = 0;
  A2D::DirichletBCInfo bcinfo;
  bcinfo.add_boundary_condition(end_label, basis);

  // Create the finite-element model
  const A2D::index_t degree = 6;
  double E = 70.0e3, nu = 0.3, q = 5.0;
  TopoOpt<degree> topo(conn, bcinfo, E, nu, q);

  // Set the geometry from the node locations
  auto elem_geo = topo.get_geometry();
  A2D::set_geo_from_hex_nodes<TopoOpt<degree>::GeoBasis>(nhex, hex, Xloc,
                                                         elem_geo);
  topo.reset_geometry();

  // Solve the problem
  topo.solve();

  // Write the problem to a vtk file
  topo.tovtk("filename.vtk");

  return 0;
}

void test_febasis() {
  using T = double;
  const A2D::index_t degree = 2;
  const A2D::index_t dim = 3;

  using Quadrature = A2D::HexGaussQuadrature<degree + 1>;
  using Space =
      A2D::FESpace<T, dim, A2D::HdivSpace<T, dim>, A2D::H1Space<T, dim, dim>>;
  using Basis = A2D::FEBasis<T, A2D::QHdivHexBasis<T, degree>,
                             A2D::LagrangeH1HexBasis<T, dim, degree>>;
  const A2D::index_t ncomp = Space::ncomp;
  using MatType = A2D::Mat<T, Basis::ndof, Basis::ndof>;
  using QMatType = A2D::Mat<T, ncomp, ncomp>;

  // Generate random data
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<> distr(-1.0, 1.0);

  // Set the degrees of freedom and basis
  T dof[Basis::ndof], res[Basis::ndof], result[Basis::ndof];
  for (A2D::index_t i = 0; i < Basis::ndof; i++) {
    dof[i] = distr(gen);
    res[i] = 0.0;
    result[i] = 0.0;
  }

  Space s, p;
  MatType mat;
  QMatType qmat;

  for (A2D::index_t i = 0; i < ncomp; i++) {
    for (A2D::index_t j = 0; j < ncomp; j++) {
      qmat(i, j) = distr(gen);
    }
  }

  A2D::index_t pt = degree + 4;
  Basis::add_outer<Quadrature>(pt, qmat, mat);

  Basis::interp_basis<Quadrature>(pt, dof, s);
  for (A2D::index_t i = 0; i < ncomp; i++) {
    p[i] = 0.0;
    for (A2D::index_t j = 0; j < ncomp; j++) {
      p[i] += qmat(i, j) * s[j];
    }
  }
  Basis::add_basis<Quadrature>(pt, p, res);

  for (A2D::index_t i = 0; i < Basis::ndof; i++) {
    for (A2D::index_t j = 0; j < Basis::ndof; j++) {
      result[i] += mat(i, j) * dof[j];
    }
  }

  std::cout << std::setw(15) << "add_outer " << std::setw(15) << "basis"
            << std::setw(15) << "rel_err" << std::endl;
  for (A2D::index_t i = 0; i < Basis::ndof; i++) {
    std::cout << std::setw(15) << result[i] << std::setw(15) << res[i]
              << std::setw(15) << (result[i] - res[i]) / result[i] << std::endl;
  }
}