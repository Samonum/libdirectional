#include <iostream>
#include <directional/drawable_field.h>
#include <directional/draw_singularities.h>
#include <directional/dual_cycles.h>
#include <directional/trivial_connection.h>
#include <directional/adjustment_to_representative.h>
#include <directional/representative_to_adjustment.h>
#include <directional/singularities.h>
#include <directional/read_trivial_field.h>
#include <directional/matching_effort.h>
#include <Eigen/Core>
#include <igl/viewer/Viewer.h>
#include <igl/read_triangle_mesh.h>
#include <igl/per_face_normals.h>
#include <igl/edge_topology.h>
#include <igl/unproject_onto_mesh.h>
#include "Main.h"


Eigen::MatrixXi F, fieldF, meshF, singF, EV, FE, EF;
Eigen::MatrixXd V, C, meshV, meshC, fieldV, fieldC, singV, singC, norm, representative;
Eigen::VectorXd adjustmentField;
Eigen::VectorXi match; 
Eigen::VectorXi indices, calculatedIndices;
Eigen::SparseMatrix<double, Eigen::RowMajor> cycles;
igl::viewer::Viewer viewer;

Eigen::MatrixXd positiveIndices(4, 3),
				negativeIndices(4,3);

int sing_mode = 0;

int N = 4;

void ConcatMeshes(const Eigen::MatrixXd &VA, const Eigen::MatrixXi &FA, const Eigen::MatrixXd &VB, const Eigen::MatrixXi &FB, Eigen::MatrixXd &V, Eigen::MatrixXi &F)
{
	V.resize(VA.rows() + VB.rows(), VA.cols());
	V << VA, VB;
	F.resize(FA.rows() + FB.rows(), FA.cols());
	F << FA, (FB.array() + VA.rows());
}

void draw_singularities()
{
	Eigen::MatrixXd spheres;
	if(sing_mode)
		directional::draw_singularities(meshV, calculatedIndices, positiveIndices, negativeIndices, .015, singV, singF, singC);
	else
		directional::draw_singularities(meshV, indices, positiveIndices, negativeIndices, .015, singV, singF, singC);

	Eigen::MatrixXd a;
	Eigen::MatrixXi b;
	ConcatMeshes(meshV, meshF, fieldV, fieldF, a, b);
	ConcatMeshes(a, b, singV, singF, V, F);

	C.resize(F.rows(), 3);
	C << meshC, fieldC, singC;
	viewer.data.clear();
	viewer.data.set_face_based(true);
	viewer.data.set_mesh(V, F);
	viewer.data.set_colors(C);
}

void draw_field()
{
	double e;
	directional::trivial_connection(meshV, meshF, cycles, indices, N, adjustmentField, e);
	std::cout << "error: " << e << std::endl;
	directional::adjustment_to_representative(meshV, meshF, EV, EF, adjustmentField, N, 0, representative);
	double r;

	Eigen::VectorXd effort;
	directional::matching_effort(meshV, meshF, EV, EF, FE, representative, N, effort);
	directional::singularities(meshV, meshF, cycles, effort, N, calculatedIndices);

	directional::drawable_field(meshV, meshF, representative, Eigen::RowVector3d(0, 0, 1), N, 0, fieldV, fieldF, fieldC);

	meshC = Eigen::RowVector3d(1, 1, 1).replicate(meshF.rows(), 1);
	draw_singularities();
}

void update_mesh()
{
	igl::edge_topology(meshV, meshF, EV, FE, EF);
	igl::per_face_normals(meshV, meshF, norm);

	directional::dual_cycles(meshF, EV, EF, cycles);
}

bool key_down(igl::viewer::Viewer& viewer, int key, int modifiers)
{
	switch (key)
	{
	case '1':
		sing_mode = 0;
		draw_singularities();
		break;
	case '2':
		sing_mode = 1;
		draw_singularities();
		break;
	case 'W':
		bool saved;
		if (sing_mode)
			saved = directional::write_trivial_field("../../data/field/trivial", meshV, meshF, calculatedIndices, N, 0);
		else
			saved = directional::write_trivial_field("../../data/field/trivial", meshV, meshF, indices, N, 0);
		if (saved)
			std::cout << "Saved mesh" << std::endl;
		else
			std::cout << "Unable to save mesh. Error: " << errno << std::endl;
		break;
	case 'R':
		double x;
		directional::read_trivial_field("../../data/field/trivial", meshV, meshF, indices, N, x);
		update_mesh();
		draw_field();
		break;
	}
	return true;
}


int main()
{
	viewer.callback_key_down = &key_down;

	std::cout <<
		"  W       Save mesh+indices" << std::endl <<
		"  R       Read mesh+indices" << std::endl <<
		"  1       Loaded indices" << std::endl <<
		"  2       Calculated indices" << std::endl;

	igl::readOBJ("../../data/chipped-torus.obj", meshV, meshF);


	positiveIndices << .25, 0, 0,
					   .5,  0, 0,
					   .75, 0, 0,
					   1,   0, 0;

	negativeIndices << 0, .25, 0,
					   0, .5,  0,
					   0, .75, 0,
					   0, 1,   0;


	update_mesh();
	indices = Eigen::VectorXi::Zero(cycles.rows());
	indices[220] = -N;
	draw_field();
	viewer.launch();
}
