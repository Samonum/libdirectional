// Copyright (C) 2016 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef POLY_VECTOR_H
#define POLY_VECTOR_H

#include <Eigen/Geometry>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <Eigen/Eigenvalues>
#include <unsupported/Eigen/Polynomials>
#include <igl/triangle_triangle_adjacency.h>
#include <igl/local_basis.h>
#include <iostream>

namespace directional
{

	IGL_INLINE void poly_vector_prepare_solvers(
		const Eigen::MatrixXd& V,          // Vertices of the mesh
		const Eigen::MatrixXi& F,          // Faces
		const Eigen::MatrixXi& TT,         // Adjacency triangle-triangle
		const Eigen::MatrixXd& B1,
		const Eigen::MatrixXd& B2,
		const Eigen::VectorXi& soft_id,    // Soft constraints face ids
		const int N,                 // Degree of the n-rosy field
		std::vector<Eigen::SimplicialLDLT<Eigen::SparseMatrix<std::complex<double>>>*>& solvers,
		std::vector<Eigen::SparseMatrix<std::complex<double>>>& As)
	{
		using namespace std;
		using namespace Eigen;

		for (int n = 0; n < N; n++)
		{
			if (solvers.size() <= n)
				solvers.push_back(new Eigen::SimplicialLDLT<Eigen::SparseMatrix<std::complex<double>>>());
			if (As.size() <= n)
				As.push_back(Eigen::SparseMatrix<std::complex<double>>());
			// Build the sparse matrix, with an energy term for each edge
			std::vector< Triplet<std::complex<double> > > t;

			int count = 0;
			for (int f = 0; f < F.rows(); ++f)
			{
				for (int ei = 0; ei < F.cols(); ++ei)
				{
					// Look up the opposite face
					int g = TT(f, ei);

					// If it is a boundary edge, it does not contribute to the energy
					if (g == -1) continue;

					// Avoid to count every edge twice
					if (f > g) continue;

					// Compute the complex representation of the common edge
					Vector3d e = (V.row(F(f, (ei + 1) % 3)) - V.row(F(f, ei)));
					Vector2d vef = Vector2d(e.dot(B1.row(f)), e.dot(B2.row(f))).normalized();
					std::complex<double> ef(vef(0), vef(1));
					Vector2d veg = Vector2d(e.dot(B1.row(g)), e.dot(B2.row(g))).normalized();
					std::complex<double> eg(veg(0), veg(1));

					// Add the term conj(f)^n*ui - conj(g)^n*uj to the energy matrix
					t.push_back(Triplet<std::complex<double> >(count, f, std::pow(std::conj(ef), N-n)));
					t.push_back(Triplet<std::complex<double> >(count, g, -1.*std::pow(std::conj(eg), N-n)));

					++count;
				}
			}
			double lambda = 10e6;
			for (int r = 0; r < soft_id.size(); ++r)
			{
				int f = soft_id(r);
				t.push_back(Triplet<std::complex<double> >(count, f, sqrt(lambda)));
				++count;
			}

			// Prepare the solver
			As[n].resize(count, F.rows());
			As[n].setFromTriplets(t.begin(), t.end());
			solvers[n]->compute(As[n].adjoint()*As[n]);
		}
	}


	IGL_INLINE void poly_vector(
		const Eigen::MatrixXd& B1,
		const Eigen::MatrixXd& B2,
		const Eigen::VectorXi& soft_id,    // Soft constraints face ids
		const Eigen::MatrixXd& soft_value, // Soft constraints 3d vectors
		const std::vector<Eigen::SimplicialLDLT<Eigen::SparseMatrix<std::complex<double>>>*>& solvers,
		const std::vector<Eigen::SparseMatrix<std::complex<double>>>& As,
		const int N,                 // Degree of the n-rosy field
		Eigen::MatrixXcd& complex)
	{
		using namespace std;
		using namespace Eigen;
		if (soft_id.size() == 0)
		{
			complex = MatrixXcd::Constant(B1.rows(), N, std::complex<double>());
			return;
		}

		// Build the sparse matrix, with an energy term for each edge
		std::vector< Triplet<std::complex<double> > > tb;

		int count = As[0].rows() - soft_id.size();

		// Convert the constraints into the complex polynomial coefficients and add them as soft constraints
		double lambda = 10e6;
		for (int r = 0; r < soft_id.size(); ++r)
		{
			int f = soft_id(r);
			Eigen::RowVectorXcd roots(N);
			for (int i = 0; i < N; i++)
			{
				Vector3d v = soft_value.block<1, 3>(r, i * 3);
				std::complex<double> c(v.dot(B1.row(f)), v.dot(B2.row(f)));
				roots(i) = c;
			}
			Eigen::VectorXcd poly;
			roots_to_monicPolynomial(roots, poly);
			for (int i = 0; i < N; i++)
			{
				tb.push_back(Triplet<std::complex<double> >(count, i, poly(i) * std::complex<double>(sqrt(lambda), 0)));
			}
			++count;
		}

		complex.resize(B1.rows(), N);
		typedef SparseMatrix<std::complex<double>> SparseMatrixXcd;
		SparseMatrixXcd b(count, N);
		b.setFromTriplets(tb.begin(), tb.end());

		for (int n = 0; n < N; n++)
		{
			// Solve the linear system
			assert(solvers[n]->info() == Success);
			complex.col(n) = solvers[n]->solve(As[n].adjoint()*MatrixXcd(b).col(n));
			assert(solvers[n]->info() == Success);
		}
	}

	// Returns a list of faces, vertices and colour values that can be used to draw a vector field 
	// on a mesh.
	// Inputs:
	//  V: #V X 3 vertex coordinates.
	//  F: #F by 3 face vertex indices.
	//  TT: #F by 3 Triangle-triangle adjecencies.
	//  B1, B2: #F by 3 matrices representing the local base of each face.
	//  N: The degree of the field..
	// Outputs:
	//  complex: Representation of the field as complex double
	IGL_INLINE void poly_vector
	(
		const Eigen::MatrixXd& V,          // Vertices of the mesh
		const Eigen::MatrixXi& F,          // Faces
		const Eigen::MatrixXi& TT,         // Adjacency triangle-triangle
		const Eigen::MatrixXd& B1,
		const Eigen::MatrixXd& B2,
		const Eigen::VectorXi& soft_id,    // Soft constraints face ids
		const Eigen::MatrixXd& soft_value, // Soft constraints 3d vectors
		const int N,                 // Degree of the n-rosy field
		Eigen::MatrixXcd& complex
	)
	{
		std::vector<Eigen::SimplicialLDLT<Eigen::SparseMatrix<std::complex<double>>>*> solvers;
		std::vector<Eigen::SparseMatrix<std::complex<double>>> As;
		poly_vector_prepare_solvers(V, F, TT, B1, B2, soft_id, N, solvers, As);
		poly_vector(B1, B2, soft_id, soft_value, solvers, As, N, complex);
		for (std::vector< Eigen::SimplicialLDLT<Eigen::SparseMatrix<std::complex<double>>>* >::iterator it = solvers.begin(); it != solvers.end(); ++it)
		{
			delete (*it);
		}
	}


	// Returns a list of faces, vertices and colour values that can be used to draw a vector field 
	// on a mesh.
	// Inputs:
	//  V: #V X 3 vertex coordinates.
	//  F: #F by 3 face vertex indices.
	//  N: The degree of the field..
	// Outputs:
	//  complex: Representation of the field as complex double
	IGL_INLINE void poly_vector
	(
		const Eigen::MatrixXd& V,          // Vertices of the mesh
		const Eigen::MatrixXi& F,          // Faces
		const Eigen::VectorXi& soft_id,    // Soft constraints face ids
		const Eigen::MatrixXd& soft_value, // Soft constraints 3d vectors
		const int N,                 // Degree of the n-rosy field
		Eigen::MatrixXcd& complex
	)
	{
		Eigen::MatrixXi TT;
		igl::triangle_triangle_adjacency(F, TT);
		Eigen::MatrixXd B1, B2, x;
		igl::local_basis(V, F, B1, B2, x);
		poly_vector(V, F, TT, B1, B2, soft_id, soft_value, N, complex);
	}
}
#endif