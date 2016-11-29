/*
 * Copyright (c) The Shogun Machine Learning Toolbox
 * Written (w) 2016 Heiko Strathmann
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are those
 * of the authors and should not be interpreted as representing official policies,
 * either expressed or implied, of the Shogun Development Team.
 */

#include <shogun/lib/config.h>
#include <shogun/lib/SGMatrix.h>
#include <shogun/lib/SGVector.h>
#include <shogun/mathematics/eigen3.h>
#include <shogun/mathematics/Math.h>
#include <shogun/io/SGIO.h>

#include "Base.h"

using namespace shogun;
using namespace shogun::kernel_exp_family_impl;
using namespace Eigen;

index_t Base::get_num_dimensions() const
{
	return m_lhs.num_rows;
}

index_t Base::get_num_lhs() const
{
	return m_lhs.num_cols;
}

void Base::set_test_data(SGMatrix<float64_t> X)
{
	m_rhs = X;
	m_kernel->set_rhs(X);
	m_kernel->precompute();
}

void Base::set_test_data(SGVector<float64_t> x)
{
	set_test_data(SGMatrix<float64_t>(x));
}

void Base::reset_test_data()
{
	set_test_data(m_lhs);
}

bool Base::is_test_equals_train_data() const
{
	// check for same memory and same dimensions
	if (m_lhs.matrix != m_rhs.matrix)
		return false;

	if (m_lhs.num_rows != m_rhs.num_rows)
		return false;

	if (m_lhs.num_cols != m_rhs.num_cols)
		return false;

	return true;
}

index_t Base::get_num_rhs() const
{
	return m_rhs.num_cols;
}

const SGVector<float64_t> Base::get_lhs_point(index_t i) const
{
	return SGVector<float64_t>(m_lhs.get_column_vector(i), get_num_dimensions(), false);
}

const SGVector<float64_t> Base::get_rhs_point(index_t i) const
{
	return SGVector<float64_t>(m_rhs.get_column_vector(i), get_num_dimensions(), false);
}

Base::Base(SGMatrix<float64_t> data,
		kernel::Base* kernel, float64_t lambda)
{
	m_lhs = data;
	m_rhs = data;
	m_kernel = kernel;
	m_kernel->set_lhs(data);
	m_kernel->set_rhs(data);
	m_lambda = lambda;

	SG_SINFO("Problem size is N=%d, D=%d.\n", get_num_lhs(), get_num_dimensions());
	m_kernel->precompute();
}

Base::~Base()
{
	delete m_kernel;
}

void Base::fit()
{
	SG_SINFO("Building system.\n");
	auto A_b = build_system();

	SG_SINFO("Solving system of size %d.\n", A_b.second.vlen);
	solve_and_store(A_b.first, A_b.second);
}

float64_t Base::objective() const
{
	// TODO check for rounding errors as Python implementation differs for Hessian diagonal
	auto N = get_num_rhs();
	auto D = get_num_dimensions();

	float64_t objective = 0.0;

#pragma omp parallel for reduction (+:objective)
	for (auto i=0; i<N; ++i)
	{
		auto gradient = ((const Base*)this)->grad(i);
		auto eigen_gradient = Map<VectorXd>(gradient.vector, D);
		objective += 0.5 * eigen_gradient.squaredNorm();

		auto hessian_diag = ((const Base*)this)->hessian_diag(i);
		auto eigen_hessian_diag = Map<VectorXd>(hessian_diag.vector, D);
		objective += eigen_hessian_diag.sum();
	}

	return objective / N;
}

void Base::solve_and_store(const SGMatrix<float64_t>& A, const SGVector<float64_t>& b)
{
	auto eigen_A = Map<MatrixXd>(A.matrix, A.num_rows, A.num_cols);
	auto eigen_b = Map<VectorXd>(b.vector, b.vlen);

	m_alpha_beta = SGVector<float64_t>(b.vlen);
	auto eigen_alpha_beta = Map<VectorXd>(m_alpha_beta.vector, m_alpha_beta.vlen);

//	SG_SINFO("Solving with LDLT.\n");
//	auto solver = LDLT<MatrixXd>();
//	solver.compute(eigen_A);
//
//	if (!solver.info() == Eigen::Success)
//		SG_SWARNING("Numerical problems computing LDLT.\n");
//
//	eigen_alpha_beta = solver.solve(eigen_b);

	// SVD seems better behaved, but it is way slower
	SG_SINFO("Solving with SVD.\n");
	JacobiSVD<MatrixXd> solver(eigen_A, Eigen::ComputeThinU | Eigen::ComputeThinV);
	eigen_alpha_beta = solver.solve(eigen_b);

	auto s = solver.singularValues().array().pow(2);
	SG_SINFO("Eigenspectrum range is [%f, %f], or [exp(%f), exp(%f)].\n",
			s.array().minCoeff(), s.array().maxCoeff(),
			CMath::log(s.array().minCoeff()), CMath::log(s.array().maxCoeff()));
}

SGVector<float64_t> Base::log_pdf() const
{
	auto N_test = get_num_rhs();
	SGVector<float64_t> result(N_test);
#pragma omp parallel for
	for (auto i=0; i<N_test; ++i)
		result[i] = ((const Base*)this)->log_pdf(i);

	return result;
}

SGMatrix<float64_t> Base::grad() const
{
	auto N_test = get_num_rhs();
	auto D = get_num_dimensions();

	SGMatrix<float64_t> result(D, N_test);
#pragma omp parallel for
	for (auto i=0; i<N_test; ++i)
	{
		auto grad = ((const Base*)this)->grad(i);
		memcpy(grad.vector, result.get_column_vector(i), D*sizeof(float64_t));
	}

	return result;
}
