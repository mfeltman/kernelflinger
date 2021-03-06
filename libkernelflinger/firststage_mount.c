/*
 * Copyright (c) 2018, Intel Corporation
 * All rights reserved.
 *
 * Author: Haoyu Tang <haoyu.tang@intel.com>
 *         Chen, ZhiminX <zhiminx.chen@intel.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <efi.h>
#include <efiapi.h>

#include "acpi.h"
#include "firststage_mount.h"
#include "firststage_mount_cfg.h"
#include "lib.h"
#include "protocol/AcpiTableProtocol.h"
#include "storage.h"

static CHAR8 csum(void *base, UINTN n)
{
	CHAR8 *p;
	CHAR8 sum;
	UINTN bytesDone;

	p = (CHAR8 *)base;

	sum = 0;
	for (bytesDone = 0; bytesDone < n; bytesDone++) {
		sum += *p;
		p++;
	}

	return sum;
}

static EFI_STATUS revise_diskbus_from_ssdt(CHAR8 *ssdt, UINTN ssdt_len)
{
	const CHAR8 *pattern = (CHAR8 *)"/0000:00:ff.ff/";
	const UINTN diskbus_sufix_len = 6; /* Sample: "ff.ff/" or "ff.f//" */
	UINTN pattern_len;
	struct ACPI_DESC_HEADER *header;
	UINTN header_len;
	CHAR8 *p, *max_end;
	PCI_DEVICE_PATH *boot_device;

	header_len = sizeof(struct ACPI_DESC_HEADER);
	if (ssdt_len < header_len) {
		error(L"ACPI: invalid parameter for revise diskbus.");
		return EFI_INVALID_PARAMETER;
	}

	/* Initialize the variables. */
	pattern_len = strlen(pattern);
	boot_device = get_boot_device();
	p = ssdt + header_len;
	max_end = ssdt + ssdt_len - pattern_len;

	/* Find and revise the diskbus. */
	while (p < max_end) {
		/* Find the diskbus. */
		if (*p != pattern[0] || memcmp(p, pattern, pattern_len)) {
			p++;
			continue;
		}

		/* Revise the diskbus. */
		p += pattern_len - diskbus_sufix_len;
		efi_snprintf(p, diskbus_sufix_len, (CHAR8 *)"%02x.%x",
			     boot_device->Device, boot_device->Function);
		p += strlen(p);
		*p++ = '/';
	}

	/* Update the header information. */
	header = (struct ACPI_DESC_HEADER *)ssdt;
	header->checksum = 0;
	header->checksum = ~csum((void *)ssdt, ssdt_len) + 1;

	return EFI_SUCCESS;
}

EFI_STATUS add_firststage_mount_ssdt(void)
{
	EFI_STATUS ret;
	EFI_ACPI_TABLE_PROTOCOL *acpi;
	UINTN ssdt_len;
	UINTN TableKey;

	static EFI_GUID gAcpiTableProtocolGuid = EFI_ACPI_TABLE_PROTOCOL_GUID;

	ssdt_len = sizeof(AmlCode);
	ret = revise_diskbus_from_ssdt((CHAR8 *)AmlCode, ssdt_len);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"ACPI: fail to revise diskbus");
		return ret;
	}

	ret = LibLocateProtocol(&gAcpiTableProtocolGuid, (void **)&acpi);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"LibLocateProtocol: gAcpiTableProtocolGuid");
		return ret;
	}
	ret = uefi_call_wrapper(acpi->InstallAcpiTable, 4, acpi, AmlCode, ssdt_len,
			  &TableKey);
	if (EFI_ERROR(ret)) {
		efi_perror(ret, L"ACPI: Failed to install acpi table");
		return ret;
	}

	return EFI_SUCCESS;
}
