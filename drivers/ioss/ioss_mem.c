/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/iommu.h>

#include "ioss_i.h"

static void *default_mem_alloc(struct ioss_device *idev,
		size_t size, dma_addr_t *daddr,
		gfp_t gfp, struct ioss_mem_allocator *alctr)
{
	return dma_alloc_coherent(ioss_idev_to_real(idev), size, daddr, gfp);
}

static void default_mem_free(struct ioss_device *idev,
		size_t size, void *addr, dma_addr_t daddr,
		struct ioss_mem_allocator *alctr)
{
	dma_free_coherent(ioss_idev_to_real(idev), size, addr, daddr);
}

static phys_addr_t default_mem_pa(struct ioss_device *idev,
		void *addr, dma_addr_t daddr,
		struct ioss_mem_allocator *alctr)
{
	return page_to_phys(
		vmalloc_to_page(addr)) | ((phys_addr_t)addr & ~PAGE_MASK);
}

struct ioss_mem_allocator ioss_default_alctr = {
	.name = "dma_alloc_coherent",
	.alloc = default_mem_alloc,
	.free = default_mem_free,
	.pa = default_mem_pa,
};

#ifdef LLCC_ENABLE
/* LLCC Memory Allocator */
#include <linux/soc/qcom/llcc-qcom.h>
#include <linux/dma-map-ops.h>
#include <linux/genalloc.h>
#include <linux/log2.h>

#define TCM_POOL_MIN_ALLOC_ORDER	ilog2(256)

static struct llcc_tcm_data *ioss_tcm_mem;
static struct gen_pool *ioss_tcm_pool;


static int tcm_iommu_remap(struct ioss_device *idev, phys_addr_t phys_addr,
			   dma_addr_t iova, size_t size, gfp_t gfp)
{
	struct device *dev = ioss_idev_to_real(idev);
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	/* Cover full pages from the page-aligned IOVA base */
	size_t map_size = PAGE_ALIGN(size + (iova & (PAGE_SIZE - 1)));
	int prot = IOMMU_READ | IOMMU_WRITE;
	size_t unmapped;
	int rc;

	unmapped = iommu_unmap(domain, iova & PAGE_MASK, map_size);
	if (unmapped != map_size) {
		ioss_dev_err(idev,
			     "IOMMU unmap of TCM failed: iova=%pad, map_size=%zu, unmapped=%zu",
			     &iova, map_size, unmapped);
		dma_unmap_resource(dev, iova, size, DMA_BIDIRECTIONAL, 0);
		return -EFAULT;
	}

	if (dev_is_dma_coherent(dev))
		prot |= IOMMU_CACHE;

	rc = iommu_map(domain, iova & PAGE_MASK, phys_addr & PAGE_MASK, map_size, prot, gfp);
	if (rc) {
		ioss_dev_err(idev,
			     "IOMMU remap of TCM failed: iova=%pad, phys=%pa, map_size=%zu, rc=%d",
			     &iova, &phys_addr, map_size, rc);
		dma_unmap_resource(dev, iova, size, DMA_BIDIRECTIONAL, 0);
		return rc;
	}

	ioss_dev_log(idev, "IOMMU remapped TCM: iova=%pad -> phys=%pa, map_size=%zu",
		     &iova, &phys_addr, map_size);

	return 0;
}

static void *tcm_map_to_iommu(struct ioss_device *idev, unsigned long tcm_addr, size_t size,
			      dma_addr_t *daddr, gfp_t gfp)
{
	struct device *dev = ioss_idev_to_real(idev);
	phys_addr_t phys_addr = gen_pool_virt_to_phys(ioss_tcm_pool, tcm_addr);
	dma_addr_t iova;

	iova = dma_map_resource(dev, phys_addr, size, DMA_BIDIRECTIONAL, 0);
	if (dma_mapping_error(dev, iova)) {
		ioss_dev_err(idev, "DMA map of TCM failed: phys=%pa, size=%zu", &phys_addr, size);
		return NULL;
	}

	ioss_dev_log(idev, "DMA mapped TCM: phys=%pa -> iova=%pad, size=%zu",
		     &phys_addr, &iova, size);

	if (tcm_iommu_remap(idev, phys_addr, iova, size, gfp))
		return NULL;

	*daddr = iova;
	return (void *)tcm_addr;
}


static void *tcm_alloc_desc(struct ioss_device *idev, size_t size, dma_addr_t *daddr,
			    gfp_t gfp, struct ioss_mem_allocator *alctr)
{
	struct genpool_data_align align_data;
	unsigned long tcm_addr;
	u32 alignment = size;

	align_data.align = alignment;
	tcm_addr = gen_pool_alloc_algo_owner(ioss_tcm_pool, size,
					     gen_pool_first_fit_align,
					     &align_data, NULL);

	if (!tcm_addr) {
		ioss_dev_err(idev, "TCM alloc failed for desc (size=%zu) - TCM exhausted", size);
		return NULL;
	}

	ioss_dev_cfg(idev, "Allocated %zu bytes TCM for descriptors", size);
	ioss_log_cfg(NULL, "TCM pool status: avail=%zu / total=%zu bytes",
		     gen_pool_avail(ioss_tcm_pool),
		     gen_pool_size(ioss_tcm_pool));

	if (!tcm_map_to_iommu(idev, tcm_addr, size, daddr, gfp)) {
		gen_pool_free(ioss_tcm_pool, tcm_addr, size);
		return NULL;
	}

	return (void *)tcm_addr;
}


static void *tcm_alloc_buffer(struct ioss_device *idev, size_t size, dma_addr_t *daddr,
			      gfp_t gfp, struct ioss_mem_allocator *alctr)
{
	unsigned long tcm_addr;

	tcm_addr = gen_pool_alloc(ioss_tcm_pool, size);
	if (!tcm_addr) {
		ioss_dev_err(idev, "TCM alloc failed for buffers (size=%zu) - TCM exhausted", size);
		return NULL;
	}

	ioss_dev_cfg(idev, "Allocated %zu bytes TCM for buffers", size);
	ioss_log_cfg(NULL, "TCM pool status: avail=%zu / total=%zu bytes",
		     gen_pool_avail(ioss_tcm_pool),
		     gen_pool_size(ioss_tcm_pool));

	if (!tcm_map_to_iommu(idev, tcm_addr, size, daddr, gfp)) {
		gen_pool_free(ioss_tcm_pool, tcm_addr, size);
		return NULL;
	}

	return (void *)tcm_addr;
}

static void tcm_mem_free(struct ioss_device *idev, size_t size, void *addr, dma_addr_t daddr,
			 struct ioss_mem_allocator *alctr)
{
	struct device *dev = ioss_idev_to_real(idev);
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	size_t map_size;

	map_size = PAGE_ALIGN(size + (daddr & (PAGE_SIZE - 1)));

	size_t unmapped = iommu_unmap(domain, daddr & PAGE_MASK, map_size);
	if (unmapped != map_size) {
		ioss_dev_err(idev, "Failed to unmap IOMMU: map_size=%zu, unmapped=%zu",
			     map_size, unmapped);
	}

	gen_pool_free(ioss_tcm_pool, (unsigned long)addr, size);
	ioss_dev_cfg(idev, "Freed %zu bytes from TCM", size);
	ioss_log_cfg(NULL, "TCM pool usage: avail=%zu / total=%zu bytes",
		     gen_pool_avail(ioss_tcm_pool),
		     gen_pool_size(ioss_tcm_pool));
}

static phys_addr_t tcm_mem_pa(struct ioss_device *idev, void *addr, dma_addr_t daddr,
			      struct ioss_mem_allocator *alctr)
{
	return gen_pool_virt_to_phys(ioss_tcm_pool, (unsigned long)addr);
}


int ioss_tcm_mem_init(void)
{
	struct llcc_tcm_data *tcm_data;
	int ret;

	tcm_data = llcc_tcm_activate();
	if (IS_ERR_OR_NULL(tcm_data)) {
		ioss_log_err(NULL, "Failed to activate TCM");
		return -EFAULT;
	}

	ioss_tcm_pool = gen_pool_create(TCM_POOL_MIN_ALLOC_ORDER, -1);
	if (!ioss_tcm_pool) {
		ioss_log_err(NULL, "Failed to create TCM pool");
		ret = -ENOMEM;
		goto err_deactivate;
	}

	ret = gen_pool_add_virt(ioss_tcm_pool, (unsigned long)tcm_data->virt_addr,
				tcm_data->phys_addr, tcm_data->mem_size, -1);
	if (ret) {
		ioss_log_err(NULL, "Failed to add TCM to pool");
		goto err_destroy_pool;
	}

	ioss_tcm_mem = tcm_data;

	ioss_log_cfg(NULL, "TCM genpool initialized: size=%zu bytes, phys=%pa, virt=%p, min_alloc_order=%d",
		     tcm_data->mem_size, &tcm_data->phys_addr, tcm_data->virt_addr,
		     TCM_POOL_MIN_ALLOC_ORDER);

	return 0;

err_destroy_pool:
	gen_pool_destroy(ioss_tcm_pool);
	ioss_tcm_pool = NULL;
err_deactivate:
	llcc_tcm_deactivate(tcm_data);
	return ret;
}


void ioss_tcm_mem_deinit(void)
{
	gen_pool_destroy(ioss_tcm_pool);
	llcc_tcm_deactivate(ioss_tcm_mem);

	ioss_log_cfg(NULL, "TCM deactivated");

	ioss_tcm_pool = NULL;
	ioss_tcm_mem = NULL;
}

struct ioss_mem_allocator ioss_tcm_desc_alctr = {
	.name = "tcm_desc_allocator",
	.alloc = tcm_alloc_desc,
	.free = tcm_mem_free,
	.pa = tcm_mem_pa,
};

struct ioss_mem_allocator ioss_tcm_buf_alctr = {
	.name = "tcm_buf_allocator",
	.alloc = tcm_alloc_buffer,
	.free = tcm_mem_free,
	.pa = tcm_mem_pa,
};

#endif
