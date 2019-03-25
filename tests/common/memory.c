/*
 * Copyright (c) 2019 HabanaLabs Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "hlthunk.h"
#include "hlthunk_tests.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>

#include <limits.h>
#include <cmocka.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

void test_map_bigger_than_4GB(void **state)
{
	struct hltests_state *tests_state = (struct hltests_state *) *state;
	struct hlthunk_hw_ip_info hw_ip;
	void *device_addr, *src_ptr, *dst_ptr;
	uint64_t host_src_addr, host_dst_addr, total_size = (1ull << 30) * 5,
		dma_size = 1 << 29, offset = 0;
	uint32_t dma_dir_down, dma_dir_up;
	int rc, fd = tests_state->fd;

	/* Sanity and memory allocation */
	rc = hlthunk_get_hw_ip_info(fd, &hw_ip);
	assert_int_equal(rc, 0);

	assert_int_equal(hw_ip.dram_enabled, 1);
	assert_in_range(dma_size, 1, hw_ip.dram_size);

	device_addr = hltests_allocate_device_mem(fd, dma_size);
	assert_non_null(device_addr);

	dma_dir_down = GOYA_DMA_HOST_TO_DRAM;
	dma_dir_up = GOYA_DMA_DRAM_TO_HOST;

	src_ptr = hltests_allocate_host_mem(fd, total_size, false);
	assert_non_null(src_ptr);
	hltests_fill_rand_values(src_ptr, total_size);
	host_src_addr = hltests_get_device_va_for_host_ptr(fd, src_ptr);

	dst_ptr = hltests_allocate_host_mem(fd, dma_size, false);
	assert_non_null(dst_ptr);
	memset(dst_ptr, 0, dma_size);
	host_dst_addr = hltests_get_device_va_for_host_ptr(fd, dst_ptr);

	while (offset < total_size) {
		/* DMA: host->device */
		hltests_dma_transfer(fd, hltests_get_dma_down_qid(fd, 0, 0), 0,
			1, (host_src_addr + offset),
			(uint64_t) (uintptr_t) device_addr, dma_size,
			dma_dir_down);

		/* DMA: device->host */
		hltests_dma_transfer(fd, hltests_get_dma_up_qid(fd, 0, 0), 0, 1,
			(uint64_t) (uintptr_t) device_addr, host_dst_addr,
			dma_size, dma_dir_up);

		/* Compare host memories */
		rc = hltests_mem_compare(
				(void *) ((uintptr_t) src_ptr + offset),
				dst_ptr, dma_size);
		assert_int_equal(rc, 0);

		offset += dma_size;
	}

	/* Cleanup */
	rc = hltests_free_host_mem(fd, dst_ptr);
	assert_int_equal(rc, 0);
	rc = hltests_free_host_mem(fd, src_ptr);
	assert_int_equal(rc, 0);

	rc = hltests_free_device_mem(fd, device_addr);
	assert_int_equal(rc, 0);
}

const struct CMUnitTest memory_tests[] = {
	cmocka_unit_test(test_map_bigger_than_4GB),
};

int main(void)
{
	char *test_names_to_run;
	int rc;

	test_names_to_run = getenv("HLTHUNK_TESTS_NAMES");
	if (test_names_to_run)
		cmocka_set_test_filter(test_names_to_run);

	rc = cmocka_run_group_tests(memory_tests, hltests_setup,
					hltests_teardown);

	return rc;
}