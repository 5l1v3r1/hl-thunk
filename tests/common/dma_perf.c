// SPDX-License-Identifier: MIT

/*
 * Copyright 2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "hlthunk.h"
#include "hlthunk_tests.h"

#include <stddef.h>
#include <limits.h>
#include <cmocka.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

static double host_sram_perf_outcome;
static double sram_host_perf_outcome;
static double host_dram_perf_outcome;
static double dram_host_perf_outcome;
static double sram_dram_perf_outcome;
static double dram_sram_perf_outcome;

static double hltests_transfer_perf(int fd, uint32_t queue_index,
			uint64_t src_addr, uint64_t dst_addr,
			uint32_t size, enum hltests_goya_dma_direction dma_dir)
{
	uint32_t offset = 0;
	void *ptr;
	struct timespec begin, end;
	struct hltests_cs_chunk execute_arr[1];
	struct hltests_pkt_info pkt_info;
	uint64_t seq = 0;
	int rc, num_of_transfers, i;
	double time_diff;

	num_of_transfers = hltests_is_simulator(fd) ? 10 : 300;
	ptr = hltests_create_cb(fd, getpagesize(), true, 0);
	assert_ptr_not_equal(ptr, NULL);
	memset(&pkt_info, 0, sizeof(pkt_info));
	pkt_info.eb = EB_FALSE;
	pkt_info.mb = MB_FALSE;
	pkt_info.dma.src_addr = src_addr;
	pkt_info.dma.dst_addr = dst_addr;
	pkt_info.dma.size = size;
	pkt_info.dma.dma_dir = dma_dir;
	offset = hltests_add_dma_pkt(fd, ptr, offset, &pkt_info);

	execute_arr[0].cb_ptr = ptr;
	execute_arr[0].cb_size = offset;
	execute_arr[0].queue_index = queue_index;

	clock_gettime(CLOCK_MONOTONIC_RAW, &begin);

	for (i = 0 ; i <= num_of_transfers ; i++) {

		rc = hltests_submit_cs(fd, NULL, 0, execute_arr,
							1, false, &seq);
		assert_int_equal(rc, 0);
	}

	rc = hltests_wait_for_cs_until_not_busy(fd, seq);
	assert_int_equal(rc, HL_WAIT_CS_STATUS_COMPLETED);

	clock_gettime(CLOCK_MONOTONIC_RAW, &end);
	time_diff = (end.tv_nsec - begin.tv_nsec) / 1000000000.0 +
						(end.tv_sec  - begin.tv_sec);

	/* return value in GB/Sec */
	return ((double)(size) * num_of_transfers / time_diff)
						/ 1024 / 1024 / 1024;

}

void hltest_host_sram_transfer_perf(void **state)
{
	struct hltests_state *tests_state = (struct hltests_state *) *state;
	struct hlthunk_hw_ip_info hw_ip;
	void *src_ptr;
	uint64_t host_addr, sram_addr;
	uint32_t size;
	int rc, fd = tests_state->fd;

	rc = hlthunk_get_hw_ip_info(fd, &hw_ip);
	assert_int_equal(rc, 0);

	sram_addr = hw_ip.sram_base_address;
	size = hw_ip.sram_size;
	src_ptr = hltests_allocate_host_mem(fd, size, true);
	assert_non_null(src_ptr);

	host_addr = hltests_get_device_va_for_host_ptr(fd, src_ptr);

	host_sram_perf_outcome = hltests_transfer_perf(fd,
			hltests_get_dma_down_qid(fd, DCORE0, STREAM0),
			host_addr, sram_addr, size, GOYA_DMA_HOST_TO_SRAM);

	hltests_free_host_mem(fd, src_ptr);
}

void hltest_sram_host_transfer_perf(void **state)
{
	struct hltests_state *tests_state = (struct hltests_state *) *state;
	struct hlthunk_hw_ip_info hw_ip;
	void *dst_ptr;
	uint64_t host_addr, sram_addr;
	uint32_t size;
	int rc, fd = tests_state->fd;

	rc = hlthunk_get_hw_ip_info(fd, &hw_ip);
	assert_int_equal(rc, 0);

	sram_addr = hw_ip.sram_base_address;
	size = hw_ip.sram_size;

	dst_ptr = hltests_allocate_host_mem(fd, size, true);
	assert_non_null(dst_ptr);

	host_addr = hltests_get_device_va_for_host_ptr(fd, dst_ptr);

	sram_host_perf_outcome = hltests_transfer_perf(fd,
			hltests_get_dma_up_qid(fd, DCORE0, STREAM0),
			sram_addr, host_addr, size, GOYA_DMA_SRAM_TO_HOST);
	hltests_free_host_mem(fd, dst_ptr);
}

void hltest_host_dram_transfer_perf(void **state)
{
	struct hltests_state *tests_state = (struct hltests_state *) *state;
	struct hlthunk_hw_ip_info hw_ip;
	void *src_ptr, *dram_addr;
	uint64_t host_addr;
	uint32_t size = 50 * 1024 * 1024;
	int rc, fd = tests_state->fd;

	rc = hlthunk_get_hw_ip_info(fd, &hw_ip);
	assert_int_equal(rc, 0);

	assert_int_equal(hw_ip.dram_enabled, 1);
	assert_in_range(size, 1, hw_ip.dram_size);
	dram_addr = hltests_allocate_device_mem(fd, size, false);
	assert_non_null(dram_addr);

	src_ptr = hltests_allocate_host_mem(fd, size, true);
	assert_non_null(src_ptr);

	host_addr = hltests_get_device_va_for_host_ptr(fd, src_ptr);

	host_dram_perf_outcome = hltests_transfer_perf(fd,
			hltests_get_dma_down_qid(fd, DCORE0, STREAM0),
			host_addr, (uint64_t) (uintptr_t) dram_addr,
			size, GOYA_DMA_HOST_TO_DRAM);
	hltests_free_host_mem(fd, src_ptr);
	hltests_free_device_mem(fd, dram_addr);
}

void hltest_dram_host_transfer_perf(void **state)
{
	struct hltests_state *tests_state = (struct hltests_state *) *state;
	struct hlthunk_hw_ip_info hw_ip;
	void *dst_ptr, *dram_addr;
	uint64_t host_addr;
	uint32_t size = 50 * 1024 * 1024;
	int rc, fd = tests_state->fd;

	rc = hlthunk_get_hw_ip_info(fd, &hw_ip);
	assert_int_equal(rc, 0);

	assert_int_equal(hw_ip.dram_enabled, 1);
	assert_in_range(size, 1, hw_ip.dram_size);
	dram_addr = hltests_allocate_device_mem(fd, size, false);
	assert_non_null(dram_addr);

	dst_ptr = hltests_allocate_host_mem(fd, size, true);
	assert_non_null(dst_ptr);

	host_addr = hltests_get_device_va_for_host_ptr(fd, dst_ptr);

	dram_host_perf_outcome = hltests_transfer_perf(fd,
			hltests_get_dma_up_qid(fd, DCORE0, STREAM0),
			(uint64_t) (uintptr_t) dram_addr, host_addr, size,
			GOYA_DMA_DRAM_TO_HOST);
	hltests_free_host_mem(fd, dst_ptr);
	hltests_free_device_mem(fd, dram_addr);
}

static uint32_t setup_lower_cb_in_sram(int fd, uint64_t src_addr,
				uint64_t dst_addr, int num_of_transfers,
				uint32_t size, uint64_t sram_addr)
{
	void *lower_cb;
	uint64_t lower_cb_device_va;
	uint32_t  lower_cb_offset = 0, i;
	struct hltests_pkt_info pkt_info;

	lower_cb = hltests_allocate_host_mem(fd, 0x2000, false);
	assert_ptr_not_equal(lower_cb, NULL);
	lower_cb_device_va = hltests_get_device_va_for_host_ptr(fd, lower_cb);

	memset(&pkt_info, 0, sizeof(pkt_info));
	pkt_info.eb = EB_FALSE;
	pkt_info.mb = MB_TRUE;
	pkt_info.dma.src_addr = src_addr;
	pkt_info.dma.dst_addr = dst_addr;
	pkt_info.dma.size = size;

	for (i = 0 ; i < num_of_transfers ; i++)
		lower_cb_offset = hltests_add_dma_pkt(fd, lower_cb,
						lower_cb_offset, &pkt_info);

	memset(&pkt_info, 0, sizeof(pkt_info));
	pkt_info.eb = EB_TRUE;
	pkt_info.mb = MB_TRUE;
	pkt_info.write_to_sob.sob_id = 0;
	pkt_info.write_to_sob.value = 1;
	pkt_info.write_to_sob.mode = SOB_ADD;
	lower_cb_offset = hltests_add_write_to_sob_pkt(fd, lower_cb,
						lower_cb_offset, &pkt_info);

	hltests_dma_transfer(fd, hltests_get_dma_down_qid(fd, DCORE0, STREAM0),
				0, 0, lower_cb_device_va, sram_addr,
				lower_cb_offset, 0);
	hltests_free_host_mem(fd, lower_cb);

	return lower_cb_offset;
}

static double indirect_transfer_perf_test(int fd, uint32_t queue_index,
					uint64_t src_addr, uint64_t dst_addr)
{
	struct hlthunk_hw_ip_info hw_ip;
	struct hltests_pkt_info pkt_info;
	struct hltests_monitor_and_fence mon_and_fence_info;
	void *cp_dma_cb, *cb;
	uint64_t sram_addr, cp_dma_cb_device_va;
	uint32_t size, cp_dma_cb_offset = 0, cb_offset = 0, lower_cb_offset;
	int rc, num_of_transfers, i;

	struct timespec begin, end;
	struct hltests_cs_chunk execute_arr[2];
	uint64_t seq = 0;
	double time_diff;

	memset(&pkt_info, 0, sizeof(pkt_info));

	rc = hlthunk_get_hw_ip_info(fd, &hw_ip);
	assert_int_equal(rc, 0);
	sram_addr = hw_ip.sram_base_address;
	size = hw_ip.sram_size - 0x3000;

	num_of_transfers = hltests_is_simulator(fd) ? 10 : 300;

	lower_cb_offset = setup_lower_cb_in_sram(fd, src_addr, dst_addr,
					num_of_transfers, size, sram_addr);

	/* Clear SOB before we start */
	cb = hltests_create_cb(fd, 0x1000, true, 0);
	assert_non_null(cb);

	memset(&pkt_info, 0, sizeof(pkt_info));
	pkt_info.set_sob.dcore_id = 0;
	pkt_info.set_sob.sob_id = 0;
	pkt_info.set_sob.value = 0;
	cb_offset = hltests_add_set_sob_pkt(fd, cb, 0, &pkt_info);
	hltests_submit_and_wait_cs(fd, cb, cb_offset,
				hltests_get_dma_down_qid(fd, 0, 0), true);

	/* Internal CB for CP_DMA */
	cp_dma_cb = hltests_create_cb(fd, 0x20, false, sram_addr + 0x2000);
	assert_non_null(cp_dma_cb);
	cp_dma_cb_device_va = hltests_get_device_va_for_host_ptr(fd, cp_dma_cb);

	pkt_info.eb = EB_FALSE;
	pkt_info.mb = MB_FALSE;
	pkt_info.cp_dma.src_addr = sram_addr;
	pkt_info.cp_dma.size = lower_cb_offset;
	cp_dma_cb_offset = hltests_add_cp_dma_pkt(fd, cp_dma_cb,
					cp_dma_cb_offset, &pkt_info);

	hltests_dma_transfer(fd, hltests_get_dma_down_qid(fd, DCORE0, DCORE0),
				0, 0, cp_dma_cb_device_va, sram_addr + 0x2000,
				cp_dma_cb_offset, 0);

	cb = hltests_create_cb(fd, 0x1000, true, 0);
	assert_non_null(cb);
	memset(&mon_and_fence_info, 0, sizeof(mon_and_fence_info));
	mon_and_fence_info.dcore_id = 0;
	mon_and_fence_info.queue_id = hltests_get_dma_down_qid(fd,
							DCORE0, STREAM0);
	mon_and_fence_info.cmdq_fence = false;
	mon_and_fence_info.sob_id = 0;
	mon_and_fence_info.mon_id = 0;
	mon_and_fence_info.mon_address = 0;
	mon_and_fence_info.target_val = 1;
	mon_and_fence_info.dec_val = 1;
	cb_offset = hltests_add_monitor_and_fence(fd, cb, 0,
						&mon_and_fence_info);

	execute_arr[0].cb_ptr = cp_dma_cb;
	execute_arr[0].cb_size = cp_dma_cb_offset;
	execute_arr[0].queue_index = queue_index;

	execute_arr[1].cb_ptr = cb;
	execute_arr[1].cb_size = cb_offset;
	execute_arr[1].queue_index = hltests_get_dma_down_qid(fd,
							DCORE0, STREAM0);

	clock_gettime(CLOCK_MONOTONIC_RAW, &begin);

	rc = hltests_submit_cs(fd, NULL, 0, execute_arr, 2, false, &seq);
	assert_int_equal(rc, 0);

	rc = hltests_wait_for_cs_until_not_busy(fd, seq);
	assert_int_equal(rc, HL_WAIT_CS_STATUS_COMPLETED);

	clock_gettime(CLOCK_MONOTONIC_RAW, &end);
	time_diff = (end.tv_nsec - begin.tv_nsec) / 1000000000.0 +
						(end.tv_sec  - begin.tv_sec);
	hltests_destroy_cb(fd, cp_dma_cb);
	hltests_destroy_cb(fd, cb);

	/* return value in GB/Sec */
	return ((double)(size) * num_of_transfers / time_diff)
							/ 1024 / 1024 / 1024;
}

void hltest_sram_dram_transfer_perf(void **state)
{
	struct hltests_state *tests_state = (struct hltests_state *) *state;
	struct hlthunk_hw_ip_info hw_ip;
	void *dram_addr;
	uint64_t sram_addr;
	uint32_t size;
	int rc, fd = tests_state->fd;

	rc = hlthunk_get_hw_ip_info(fd, &hw_ip);
	assert_int_equal(rc, 0);

	sram_addr = hw_ip.sram_base_address;
	size = hw_ip.sram_size;
	assert_int_equal(hw_ip.dram_enabled, 1);
	assert_in_range(size, 1, hw_ip.dram_size);
	dram_addr = hltests_allocate_device_mem(fd, size, false);
	assert_non_null(dram_addr);

	if (hltests_is_goya(fd))
		sram_dram_perf_outcome = hltests_transfer_perf(fd,
			hltests_get_dma_sram_to_dram_qid(fd, DCORE0, STREAM0),
			sram_addr, (uint64_t) (uintptr_t) dram_addr, size,
			GOYA_DMA_SRAM_TO_DRAM);
	else
		sram_dram_perf_outcome =
			indirect_transfer_perf_test(fd,
			hltests_get_dma_sram_to_dram_qid(fd, DCORE0, STREAM0),
			sram_addr + 0x3000, (uint64_t) (uintptr_t) dram_addr);

	hltests_free_device_mem(fd, dram_addr);
}

void hltest_dram_sram_transfer_perf(void **state)
{
	struct hltests_state *tests_state = (struct hltests_state *) *state;
	struct hlthunk_hw_ip_info hw_ip;
	void *dram_addr;
	uint64_t sram_addr;
	uint32_t size;
	int rc, fd = tests_state->fd;

	rc = hlthunk_get_hw_ip_info(fd, &hw_ip);
	assert_int_equal(rc, 0);

	sram_addr = hw_ip.sram_base_address;
	size = hw_ip.sram_size;

	assert_int_equal(hw_ip.dram_enabled, 1);
	assert_in_range(size, 1, hw_ip.dram_size);
	dram_addr = hltests_allocate_device_mem(fd, size, false);
	assert_non_null(dram_addr);

	if (hltests_is_goya(fd))
		dram_sram_perf_outcome = hltests_transfer_perf(fd,
			hltests_get_dma_dram_to_sram_qid(fd, DCORE0, STREAM0),
			(uint64_t) (uintptr_t) dram_addr, sram_addr,
			size, GOYA_DMA_DRAM_TO_SRAM);
	else
		dram_sram_perf_outcome =
			indirect_transfer_perf_test(fd,
			hltests_get_dma_dram_to_sram_qid(fd, DCORE0, STREAM0),
			(uint64_t) (uintptr_t) dram_addr, sram_addr + 0x3000);

	hltests_free_device_mem(fd, dram_addr);
}

const struct CMUnitTest dma_perf_tests[] = {
	cmocka_unit_test_setup(hltest_host_sram_transfer_perf,
				hltests_ensure_device_operational),
	cmocka_unit_test_setup(hltest_sram_host_transfer_perf,
				hltests_ensure_device_operational),
	cmocka_unit_test_setup(hltest_host_dram_transfer_perf,
				hltests_ensure_device_operational),
	cmocka_unit_test_setup(hltest_dram_host_transfer_perf,
				hltests_ensure_device_operational),
	cmocka_unit_test_setup(hltest_sram_dram_transfer_perf,
				hltests_ensure_device_operational),
	cmocka_unit_test_setup(hltest_dram_sram_transfer_perf,
				hltests_ensure_device_operational),
};

static const char *const usage[] = {
	"dma_perf [options]",
	NULL,
};

int main(int argc, const char **argv)
{
	int rc;

	hltests_parser(argc, argv, usage, HLTHUNK_DEVICE_INVALID,
			dma_perf_tests,
			sizeof(dma_perf_tests) / sizeof((dma_perf_tests)[0]));

	rc = cmocka_run_group_tests(dma_perf_tests, hltests_setup,
					hltests_teardown);

	if (!rc) {
		printf("========\n");
		printf("RESULTS:\n");
		printf("========\n");
		printf("HOST->SRAM %lf GB/Sec\n", host_sram_perf_outcome);
		printf("SRAM->HOST %lf GB/Sec\n", sram_host_perf_outcome);
		printf("HOST->DRAM %lf GB/Sec\n", host_dram_perf_outcome);
		printf("DRAM->HOST %lf GB/Sec\n", dram_host_perf_outcome);
		printf("SRAM->DRAM %lf GB/Sec\n", sram_dram_perf_outcome);
		printf("DRAM->SRAM %lf GB/Sec\n", dram_sram_perf_outcome);
	}

	return rc;
}

