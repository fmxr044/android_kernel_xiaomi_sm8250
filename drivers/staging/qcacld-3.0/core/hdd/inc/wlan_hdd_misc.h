/*
 * Copyright (c) 2012-2014,2016-2017,2019 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef WLAN_HDD_MISC_H
#define WLAN_HDD_MISC_H
/*
 * To prevent name conflicts when loading different instances of the driver:
 *
 * If DYNAMIC_SINGLE_CHIP is defined, which means there are multiple possible
 * drivers, but only one instance of driver at a time(WLAN dynamic detect),
 * prepend DYNAMIC_SINGLE_CHIP to the filenames.
 *
 * Otherwise, if MULTI_IF_NAME is defined, which means there are multiple
 * instances of the driver with different module names, prepend MULTI_IF_NAME
 * to the filenames.
 */
#ifdef DYNAMIC_SINGLE_CHIP
#define PREFIX DYNAMIC_SINGLE_CHIP "/"
#else

#ifdef MULTI_IF_NAME
#define PREFIX MULTI_IF_NAME "/"
#else
#define PREFIX ""
#endif

#endif

#ifdef MSM_PLATFORM
#define WLAN_INI_FILE              "wlan/qca_cld/" PREFIX "WCNSS_qcom_cfg.ini"
#define WLAN_MAC_FILE              "wlan/qca_cld/" PREFIX "wlan_mac.bin"
#else
#define WLAN_INI_FILE              "wlan/" PREFIX "qcom_cfg.ini"
#define WLAN_MAC_FILE              "wlan/" PREFIX "wlan_mac.bin"
#endif /* MSM_PLATFORM */

/*
 * 备用路径前缀：芯片子目录。
 *
 * sm8250(munch) 上原厂 host driver 是以芯片名作为模块名构建的
 * （/vendor/lib/modules/qca_cld3_qca6390.ko），MULTI_IF_NAME 为 qca6390，
 * 所以设备上的 ini/mac 文件实际位于 wlan/qca_cld/qca6390/ 目录下。
 *
 * 本树把 wlan 驱动内建进内核（obj-y），MODNAME 为 wlan，MULTI_IF_NAME 未定义，
 * PREFIX 为空，上面的 WLAN_INI_FILE 会解析成 wlan/qca_cld/WCNSS_qcom_cfg.ini。
 * 该文件在设备上不存在，request_firmware() 失败会让 hdd_context_create()
 * 直接返回失败，进而 cnss probe 失败、Wi-Fi 完全起不来（无 wlan0、热点打不开）。
 *
 * 因此这里给出与芯片名一致的备用路径，主路径读不到时回退过去。
 */
#ifdef MSM_PLATFORM
#if defined(CONFIG_CNSS_QCA6390)
#define WLAN_CHIP_DIR              "qca6390/"
#else
#define WLAN_CHIP_DIR              ""
#endif
#define WLAN_INI_FILE_FALLBACK \
	"wlan/qca_cld/" WLAN_CHIP_DIR "WCNSS_qcom_cfg.ini"
#define WLAN_MAC_FILE_FALLBACK \
	"wlan/qca_cld/" WLAN_CHIP_DIR "wlan_mac.bin"
#else
#define WLAN_INI_FILE_FALLBACK     WLAN_INI_FILE
#define WLAN_MAC_FILE_FALLBACK     WLAN_MAC_FILE
#endif /* MSM_PLATFORM */

#endif /* WLAN_HDD_MISC_H */
