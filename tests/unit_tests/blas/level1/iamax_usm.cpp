/*******************************************************************************
* Copyright 2020-2021 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing,
* software distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions
* and limitations under the License.
*
*
* SPDX-License-Identifier: Apache-2.0
*******************************************************************************/

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

#if __has_include(<sycl/sycl.hpp>)
#include <sycl/sycl.hpp>
#else
#include <CL/sycl.hpp>
#endif
#include "cblas.h"
#include "oneapi/mkl/detail/config.hpp"
#include "oneapi/mkl.hpp"
#include "onemkl_blas_helper.hpp"
#include "reference_blas_templates.hpp"
#include "test_common.hpp"
#include "test_helper.hpp"

#include <gtest/gtest.h>

using namespace sycl;
using std::vector;

extern std::vector<sycl::device*> devices;

namespace {

template <typename fp, usm::alloc alloc_type = usm::alloc::shared>
int test(device* dev, oneapi::mkl::layout layout, int N, int incx) {
    // Catch asynchronous exceptions.
    auto exception_handler = [](exception_list exceptions) {
        for (std::exception_ptr const& e : exceptions) {
            try {
                std::rethrow_exception(e);
            }
            catch (exception const& e) {
                std::cout << "Caught asynchronous SYCL exception during IAMAX:\n"
                          << e.what() << std::endl;
                print_error_code(e);
            }
        }
    };

    queue main_queue(*dev, exception_handler);
    context cxt = main_queue.get_context();
    event done;
    std::vector<event> dependencies;

    // Prepare data.
    auto ua = usm_allocator<fp, usm::alloc::shared, 64>(cxt, *dev);
    vector<fp, decltype(ua)> x(ua);
    int64_t result_ref = -1;
    rand_vector(x, N, incx);

    // Call Reference IAMAX.
    using fp_ref = typename ref_type_info<fp>::type;
    const int N_ref = N, incx_ref = incx;

    result_ref = ::iamax(&N_ref, (fp_ref*)x.data(), &incx_ref);

    // Call DPC++ IAMAX.

    int64_t* result_p;
    if constexpr (alloc_type == usm::alloc::shared) {
        result_p = (int64_t*)oneapi::mkl::malloc_shared(64, sizeof(int64_t), *dev, cxt);
    }
    else if constexpr (alloc_type == usm::alloc::device) {
        result_p = (int64_t*)oneapi::mkl::malloc_device(64, sizeof(int64_t), *dev, cxt);
    }
    else {
        throw std::runtime_error("Bad alloc_type");
    }

    try {
#ifdef CALL_RT_API
        switch (layout) {
            case oneapi::mkl::layout::col_major:
                done = oneapi::mkl::blas::column_major::iamax(main_queue, N, x.data(), incx,
                                                              result_p, dependencies);
                break;
            case oneapi::mkl::layout::row_major:
                done = oneapi::mkl::blas::row_major::iamax(main_queue, N, x.data(), incx, result_p,
                                                           dependencies);
                break;
            default: break;
        }
        done.wait();
#else
        switch (layout) {
            case oneapi::mkl::layout::col_major:
                TEST_RUN_CT_SELECT(main_queue, oneapi::mkl::blas::column_major::iamax, N, x.data(),
                                   incx, result_p, dependencies);
                break;
            case oneapi::mkl::layout::row_major:
                TEST_RUN_CT_SELECT(main_queue, oneapi::mkl::blas::row_major::iamax, N, x.data(),
                                   incx, result_p, dependencies);
                break;
            default: break;
        }
        main_queue.wait();
#endif
    }
    catch (exception const& e) {
        std::cout << "Caught synchronous SYCL exception during IAMAX:\n" << e.what() << std::endl;
        print_error_code(e);
    }

    catch (const oneapi::mkl::unimplemented& e) {
        return test_skipped;
    }

    catch (const std::runtime_error& error) {
        std::cout << "Error raised during execution of IAMAX:\n" << error.what() << std::endl;
    }

    // Compare the results of reference implementation and DPC++ implementation.

    bool good = check_equal_ptr(main_queue, result_p, result_ref, 0, std::cout);
    oneapi::mkl::free_usm(result_p, cxt);

    return (int)good;
}

class IamaxUsmTests
        : public ::testing::TestWithParam<std::tuple<sycl::device*, oneapi::mkl::layout>> {};

TEST_P(IamaxUsmTests, RealSinglePrecision) {
    EXPECT_TRUEORSKIP(test<float>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, 2));
    EXPECT_TRUEORSKIP(test<float>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, 1));
    EXPECT_TRUEORSKIP((
        test<float, usm::alloc::device>(std::get<0>(GetParam()), std::get<1>(GetParam()), 101, 1)));
    EXPECT_TRUEORSKIP(test<float>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, -3));
}
TEST_P(IamaxUsmTests, RealDoublePrecision) {
    CHECK_DOUBLE_ON_DEVICE(std::get<0>(GetParam()));

    EXPECT_TRUEORSKIP(test<double>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, 2));
    EXPECT_TRUEORSKIP(test<double>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, 1));
    EXPECT_TRUEORSKIP((test<double, usm::alloc::device>(std::get<0>(GetParam()),
                                                        std::get<1>(GetParam()), 101, 1)));
    EXPECT_TRUEORSKIP(test<double>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, -3));
}
TEST_P(IamaxUsmTests, ComplexSinglePrecision) {
    EXPECT_TRUEORSKIP(
        test<std::complex<float>>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, 2));
    EXPECT_TRUEORSKIP(
        test<std::complex<float>>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, 1));
    EXPECT_TRUEORSKIP((test<std::complex<float>, usm::alloc::device>(
        std::get<0>(GetParam()), std::get<1>(GetParam()), 101, 1)));
    EXPECT_TRUEORSKIP(
        test<std::complex<float>>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, -3));
}
TEST_P(IamaxUsmTests, ComplexDoublePrecision) {
    CHECK_DOUBLE_ON_DEVICE(std::get<0>(GetParam()));

    EXPECT_TRUEORSKIP(
        test<std::complex<double>>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, 2));
    EXPECT_TRUEORSKIP(
        test<std::complex<double>>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, 1));
    EXPECT_TRUEORSKIP((test<std::complex<double>, usm::alloc::device>(
        std::get<0>(GetParam()), std::get<1>(GetParam()), 101, 1)));
    EXPECT_TRUEORSKIP(
        test<std::complex<double>>(std::get<0>(GetParam()), std::get<1>(GetParam()), 1357, -3));
}

INSTANTIATE_TEST_SUITE_P(IamaxUsmTestSuite, IamaxUsmTests,
                         ::testing::Combine(testing::ValuesIn(devices),
                                            testing::Values(oneapi::mkl::layout::col_major,
                                                            oneapi::mkl::layout::row_major)),
                         ::LayoutDeviceNamePrint());

} // anonymous namespace
