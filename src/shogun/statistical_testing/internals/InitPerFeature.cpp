/*
 * Restructuring Shogun's statistical hypothesis testing framework.
 * Copyright (C) 2014  Soumyajit De
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http:/www.gnu.org/licenses/>.
 */

#include <shogun/statistical_testing/internals/InitPerFeature.h>
#include <shogun/statistical_testing/internals/DataFetcher.h>
#include <shogun/statistical_testing/internals/DataFetcherFactory.h>
#include <shogun/features/Features.h>

using namespace shogun;
using namespace internal;

InitPerFeature::InitPerFeature(std::unique_ptr<DataFetcher>& fetcher) : m_fetcher(fetcher)
{
}

InitPerFeature::~InitPerFeature()
{
}

InitPerFeature& InitPerFeature::operator=(CFeatures* feats)
{
	m_fetcher = std::unique_ptr<DataFetcher>(DataFetcherFactory::get_instance(feats));
	return *this;
}

InitPerFeature::operator const CFeatures*() const
{
	return m_fetcher->m_samples;
}
