#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <net/if.h>
#include <linux/spi/spidev.h>
#include <linux/input.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/nl80211.h>
#include <linux/loop.h>
#include <linux/dm-ioctl.h>

/* ==================== Bluetooth HCI UP ==================== */

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#define BTPROTO_HCI 1
#define HCIDEVUP _IOW('H', 201, int)
#define HCIGETDEVINFO _IOR('H', 211, int)

struct hci_dev_info {
    uint16_t dev_id;
    char name[8];
    uint8_t bdaddr[6];
    uint32_t flags;
    uint8_t type;
    uint8_t features[8];
    uint32_t pkt_type;
    uint32_t link_policy;
    uint32_t link_mode;
    uint16_t acl_mtu;
    uint16_t acl_pkts;
    uint16_t sco_mtu;
    uint16_t sco_pkts;
    uint32_t stat[10];
};

static int cmd_bt_up(int dev_id) {
    int s = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
    if (s < 0) {
        fprintf(stderr, "BT socket failed: %s\n", strerror(errno));
        return 1;
    }
    if (ioctl(s, HCIDEVUP, dev_id) < 0 && errno != EALREADY) {
        fprintf(stderr, "HCIDEVUP hci%d failed: %s\n", dev_id, strerror(errno));
        close(s);
        return 2;
    }
    struct hci_dev_info di;
    memset(&di, 0, sizeof(di));
    di.dev_id = dev_id;
    if (ioctl(s, HCIGETDEVINFO, &di) == 0) {
        printf("hci%d UP! name=%s bdaddr=%02X:%02X:%02X:%02X:%02X:%02X flags=0x%x\n",
               dev_id, di.name,
               di.bdaddr[5], di.bdaddr[4], di.bdaddr[3],
               di.bdaddr[2], di.bdaddr[1], di.bdaddr[0], di.flags);
    } else {
        printf("hci%d UP! (HCIGETDEVINFO: %s)\n", dev_id, strerror(errno));
    }
    close(s);
    return 0;
}

/* ==================== Live Touchscreen Watch ==================== */

struct mt_slot_state {
    int tracking_id;
    int x;
    int y;
    int major;
    int pressure;
    int dirty;
};

static int find_touch_event_dev(char *out, size_t out_sz) {
    for (int i = 0; i < 16; i++) {
        char path[64], name[128];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        memset(name, 0, sizeof(name));
        if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0) {
            if (strstr(name, "Synaptics") || strstr(name, "Touch") || strstr(name, "TCM")) {
                close(fd);
                snprintf(out, out_sz, "%s", path);
                return 0;
            }
        }
        close(fd);
    }
    return -1;
}

static int cmd_touch_watch(const char *arg_path) {
    char path[64];
    if (arg_path && arg_path[0]) {
        snprintf(path, sizeof(path), "%s", arg_path);
    } else if (find_touch_event_dev(path, sizeof(path)) < 0) {
        fprintf(stderr, "No Synaptics/Touch /dev/input/event* device found\n");
        return 1;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", path, strerror(errno));
        return 1;
    }

    char name[128] = {0};
    struct input_absinfo abs_x = {0}, abs_y = {0};
    ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
    ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_x);
    ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y);
    printf("Touch watch on %s: '%s' (X=%d..%d, Y=%d..%d)\n",
           path, name, abs_x.minimum, abs_x.maximum, abs_y.minimum, abs_y.maximum);
    printf("Touch the screen now (Ctrl-C to exit)...\n");
    fflush(stdout);

    struct mt_slot_state slots[10];
    for (int i = 0; i < 10; i++) {
        memset(&slots[i], 0, sizeof(slots[i]));
        slots[i].tracking_id = -1;
    }
    int cur_slot = 0;
    int prev_active = 0;

    struct input_event ev;
    while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS) {
            switch (ev.code) {
            case ABS_MT_SLOT:
                if (ev.value >= 0 && ev.value < 10)
                    cur_slot = ev.value;
                break;
            case ABS_MT_TRACKING_ID:
                slots[cur_slot].tracking_id = ev.value;
                slots[cur_slot].dirty = 1;
                break;
            case ABS_MT_POSITION_X:
                slots[cur_slot].x = ev.value;
                slots[cur_slot].dirty = 1;
                break;
            case ABS_MT_POSITION_Y:
                slots[cur_slot].y = ev.value;
                slots[cur_slot].dirty = 1;
                break;
            case ABS_MT_TOUCH_MAJOR:
                slots[cur_slot].major = ev.value;
                break;
            case ABS_MT_PRESSURE:
                slots[cur_slot].pressure = ev.value;
                break;
            }
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            int active = 0;
            for (int i = 0; i < 10; i++) {
                if (slots[i].tracking_id >= 0) {
                    active++;
                    printf("[TOUCH] slot=%d id=%d X=%d Y=%d (px %d,%d) maj=%d p=%d\n",
                           i, slots[i].tracking_id,
                           slots[i].x, slots[i].y,
                           slots[i].x / 10, slots[i].y / 10,
                           slots[i].major, slots[i].pressure);
                }
                slots[i].dirty = 0;
            }
            if (active == 0 && prev_active > 0)
                printf("[TOUCH] ALL FINGERS LIFTED\n");
            prev_active = active;
            fflush(stdout);
        }
    }

    close(fd);
    return 0;
}

/* ==================== Wi-Fi 7 nl80211 Scan ==================== */

static int nla_put(uint8_t *buf, int pos, uint16_t type, const void *data, int len) {
    struct nlattr *nla = (struct nlattr *)(buf + pos);
    nla->nla_len = NLA_HDRLEN + len;
    nla->nla_type = type;
    if (len > 0 && data)
        memcpy(buf + pos + NLA_HDRLEN, data, len);
    return pos + NLA_ALIGN(nla->nla_len);
}

static int nl_send_recv(int fd, uint8_t *msg, int msg_len,
                        uint8_t *resp, size_t resp_sz) {
    struct sockaddr_nl sa;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    if (sendto(fd, msg, msg_len, 0, (struct sockaddr *)&sa, sizeof(sa)) < 0)
        return -errno;
    ssize_t n = recv(fd, resp, resp_sz, 0);
    if (n < 0)
        return -errno;
    return (int)n;
}

static int nl80211_get_family_id(int fd) {
    uint8_t buf[256], resp[4096];
    memset(buf, 0, sizeof(buf));
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    struct genlmsghdr *gh = (struct genlmsghdr *)(buf + NLMSG_HDRLEN);
    int pos = NLMSG_HDRLEN + GENL_HDRLEN;

    nlh->nlmsg_type = GENL_ID_CTRL;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_seq = 1;
    gh->cmd = CTRL_CMD_GETFAMILY;
    gh->version = 1;

    pos = nla_put(buf, pos, CTRL_ATTR_FAMILY_NAME, "nl80211", 8);
    nlh->nlmsg_len = pos;

    int n = nl_send_recv(fd, buf, pos, resp, sizeof(resp));
    if (n < 0) return n;

    struct nlmsghdr *r = (struct nlmsghdr *)resp;
    if (r->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *e = (struct nlmsgerr *)NLMSG_DATA(r);
        return e->error ? e->error : -ENOENT;
    }

    int off = NLMSG_HDRLEN + GENL_HDRLEN;
    int end = r->nlmsg_len;
    while (off + (int)sizeof(struct nlattr) <= end) {
        struct nlattr *a = (struct nlattr *)(resp + off);
        if (a->nla_len < NLA_HDRLEN) break;
        if (a->nla_type == CTRL_ATTR_FAMILY_ID && a->nla_len >= NLA_HDRLEN + 2) {
            uint16_t fid;
            memcpy(&fid, (uint8_t *)a + NLA_HDRLEN, 2);
            return fid;
        }
        off += NLA_ALIGN(a->nla_len);
    }
    return -ENOENT;
}

static void parse_and_print_bss(const uint8_t *data, int len, int *count) {
    uint8_t bssid[6] = {0};
    int has_bssid = 0;
    uint32_t freq = 0;
    int32_t sig_mbm = 0;
    char ssid[65] = "(hidden)";

    int off = 0;
    while (off + (int)sizeof(struct nlattr) <= len) {
        struct nlattr *a = (struct nlattr *)(data + off);
        if (a->nla_len < NLA_HDRLEN || off + a->nla_len > len) break;
        const uint8_t *v = data + off + NLA_HDRLEN;
        int vlen = a->nla_len - NLA_HDRLEN;

        switch (a->nla_type) {
        case NL80211_BSS_BSSID:
            if (vlen >= 6) {
                memcpy(bssid, v, 6);
                has_bssid = 1;
            }
            break;
        case NL80211_BSS_FREQUENCY:
            if (vlen >= 4) memcpy(&freq, v, 4);
            break;
        case NL80211_BSS_SIGNAL_MBM:
            if (vlen >= 4) memcpy(&sig_mbm, v, 4);
            break;
        case NL80211_BSS_INFORMATION_ELEMENTS: {
            int p = 0;
            while (p + 2 <= vlen) {
                uint8_t id = v[p], elen = v[p + 1];
                if (p + 2 + elen > vlen) break;
                if (id == 0 && elen > 0) {
                    int n = elen > 64 ? 64 : elen;
                    memcpy(ssid, v + p + 2, n);
                    ssid[n] = '\0';
                    break;
                }
                p += 2 + elen;
            }
            break;
        }
        }
        off += NLA_ALIGN(a->nla_len);
    }

    if (has_bssid) {
        (*count)++;
        printf("  %02x:%02x:%02x:%02x:%02x:%02x  %4u MHz  %4d dBm  %s\n",
               bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
               freq, sig_mbm / 100, ssid);
    }
}

static int cmd_wifi_scan(const char *iface) {
    if (!iface || !iface[0]) iface = "wlan0";
    unsigned int ifidx = if_nametoindex(iface);
    if (!ifidx) {
        fprintf(stderr, "Interface '%s' not found (%s)\n", iface, strerror(errno));
        return 1;
    }

    /* Ensure interface is UP */
    int io = socket(AF_INET, SOCK_DGRAM, 0);
    if (io >= 0) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", iface);
        if (ioctl(io, SIOCGIFFLAGS, &ifr) == 0 && !(ifr.ifr_flags & IFF_UP)) {
            ifr.ifr_flags |= IFF_UP;
            if (ioctl(io, SIOCSIFFLAGS, &ifr) < 0)
                fprintf(stderr, "IFF_UP on %s failed: %s\n", iface, strerror(errno));
            else
                usleep(300000);
        }
        close(io);
    }

    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
    if (fd < 0) {
        fprintf(stderr, "netlink socket failed: %s\n", strerror(errno));
        return 1;
    }
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_nl sa;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    bind(fd, (struct sockaddr *)&sa, sizeof(sa));

    int fid = nl80211_get_family_id(fd);
    if (fid < 0) {
        fprintf(stderr, "nl80211 family lookup failed: %d\n", fid);
        close(fd);
        return 1;
    }

    /* Trigger scan */
    uint8_t buf[256], resp[16384];
    memset(buf, 0, sizeof(buf));
    struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
    struct genlmsghdr *gh = (struct genlmsghdr *)(buf + NLMSG_HDRLEN);
    int pos = NLMSG_HDRLEN + GENL_HDRLEN;

    nlh->nlmsg_type = fid;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_seq = 2;
    gh->cmd = NL80211_CMD_TRIGGER_SCAN;
    gh->version = 1;
    pos = nla_put(buf, pos, NL80211_ATTR_IFINDEX, &ifidx, 4);
    nlh->nlmsg_len = pos;

    printf("Triggering Wi-Fi scan on %s (ifindex=%u)...\n", iface, ifidx);
    int n = nl_send_recv(fd, buf, pos, resp, sizeof(resp));
    if (n > 0) {
        struct nlmsghdr *r = (struct nlmsghdr *)resp;
        if (r->nlmsg_type == NLMSG_ERROR) {
            struct nlmsgerr *e = (struct nlmsgerr *)NLMSG_DATA(r);
            if (e->error && e->error != -EBUSY)
                fprintf(stderr, "TRIGGER_SCAN returned %d (%s)\n", e->error, strerror(-e->error));
        }
    }

    /* Wait for scan to complete */
    sleep(4);

    /* Dump scan results */
    memset(buf, 0, sizeof(buf));
    nlh = (struct nlmsghdr *)buf;
    gh = (struct genlmsghdr *)(buf + NLMSG_HDRLEN);
    pos = NLMSG_HDRLEN + GENL_HDRLEN;
    nlh->nlmsg_type = fid;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    nlh->nlmsg_seq = 3;
    gh->cmd = NL80211_CMD_GET_SCAN;
    gh->version = 1;
    pos = nla_put(buf, pos, NL80211_ATTR_IFINDEX, &ifidx, 4);
    nlh->nlmsg_len = pos;

    struct sockaddr_nl dst;
    memset(&dst, 0, sizeof(dst));
    dst.nl_family = AF_NETLINK;
    if (sendto(fd, buf, pos, 0, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        fprintf(stderr, "GET_SCAN send failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    int count = 0;
    printf("Wi-Fi Scan Results (%s):\n", iface);
    for (int done = 0; !done; ) {
        ssize_t len = recv(fd, resp, sizeof(resp), 0);
        if (len <= 0) break;
        for (struct nlmsghdr *r = (struct nlmsghdr *)resp;
             NLMSG_OK(r, len); r = NLMSG_NEXT(r, len)) {
            if (r->nlmsg_type == NLMSG_DONE) {
                done = 1;
                break;
            }
            if (r->nlmsg_type == NLMSG_ERROR) {
                done = 1;
                break;
            }
            int off = NLMSG_HDRLEN + GENL_HDRLEN;
            int end = r->nlmsg_len;
            while (off + (int)sizeof(struct nlattr) <= end) {
                struct nlattr *a = (struct nlattr *)((uint8_t *)r + off);
                if (a->nla_len < NLA_HDRLEN) break;
                if ((a->nla_type & NLA_TYPE_MASK) == NL80211_ATTR_BSS) {
                    parse_and_print_bss((uint8_t *)a + NLA_HDRLEN,
                                        a->nla_len - NLA_HDRLEN, &count);
                }
                off += NLA_ALIGN(a->nla_len);
            }
        }
    }
    printf("Total APs found: %d\n", count);
    close(fd);
    return 0;
}

/* ==================== Android super -> vendor_b EROFS Mount ==================== */

#define LP_GEOMETRY_MAGIC 0x616c4467U
#define LP_HEADER_MAGIC   0x414c5030U

struct __attribute__((packed)) lp_geometry {
    uint32_t magic;
    uint32_t struct_size;
    uint8_t  checksum[32];
    uint32_t metadata_max_size;
    uint32_t metadata_slot_count;
    uint32_t logical_block_size;
};

struct __attribute__((packed)) lp_table_desc {
    uint32_t offset;
    uint32_t num_entries;
    uint32_t entry_size;
};

struct __attribute__((packed)) lp_header {
    uint32_t magic;
    uint16_t major_version;
    uint16_t minor_version;
    uint32_t header_size;
    uint8_t  header_checksum[32];
    uint32_t tables_size;
    uint8_t  tables_checksum[32];
    struct lp_table_desc partitions;
    struct lp_table_desc extents;
    struct lp_table_desc groups;
    struct lp_table_desc block_devices;
};

struct __attribute__((packed)) lp_partition {
    char     name[36];
    uint32_t attributes;
    uint32_t first_extent_index;
    uint32_t num_extents;
    uint32_t group_index;
};

struct __attribute__((packed)) lp_extent {
    uint64_t num_sectors;
    uint32_t target_type;
    uint64_t target_data;
    uint32_t target_source;
};

static int find_part_by_name(const char *partname, char *dev_out, size_t out_sz) {
    DIR *bd = opendir("/sys/block");
    if (!bd) return -1;
    struct dirent *de;
    char needle[128];
    snprintf(needle, sizeof(needle), "PARTNAME=%s\n", partname);

    while ((de = readdir(bd)) != NULL) {
        if (strncmp(de->d_name, "sd", 2) != 0) continue;
        char sdir[300];
        int sn = snprintf(sdir, sizeof(sdir), "/sys/block/%s", de->d_name);
        if (sn < 0 || (size_t)sn >= sizeof(sdir)) continue;
        DIR *pd = opendir(sdir);
        if (!pd) continue;
        struct dirent *pe;
        while ((pe = readdir(pd)) != NULL) {
            if (strncmp(pe->d_name, de->d_name, strlen(de->d_name)) != 0) continue;
            char upath[600], buf[1024];
            int un = snprintf(upath, sizeof(upath), "%s/%s/uevent", sdir, pe->d_name);
            if (un < 0 || (size_t)un >= sizeof(upath)) continue;
            int fd = open(upath, O_RDONLY);
            if (fd < 0) continue;
            ssize_t n = read(fd, buf, sizeof(buf) - 1);
            close(fd);
            if (n > 0) {
                buf[n] = '\0';
                if (strstr(buf, needle)) {
                    int dn = snprintf(dev_out, out_sz, "/dev/%s", pe->d_name);
                    if (dn < 0 || (size_t)dn >= out_sz) continue;
                    closedir(pd);
                    closedir(bd);
                    return 0;
                }
            }
        }
        closedir(pd);
    }
    closedir(bd);
    return -1;
}

static int setup_dm_linear(const char *dm_name, dev_t super_rdev,
                           const struct lp_extent *exts, uint32_t num_exts,
                           char *dm_dev_out, size_t out_sz) {
    int cfd = open("/dev/mapper/control", O_RDWR);
    if (cfd < 0) {
        mkdir("/dev/mapper", 0755);
        mknod("/dev/mapper/control", S_IFCHR | 0600, makedev(10, 236));
        cfd = open("/dev/mapper/control", O_RDWR);
        if (cfd < 0) return -errno;
    }

    uint8_t buf[16384];
    struct dm_ioctl *dmi = (struct dm_ioctl *)buf;

    /* 1. DM_DEV_CREATE */
    memset(buf, 0, sizeof(buf));
    dmi->version[0] = DM_VERSION_MAJOR;
    dmi->version[1] = DM_VERSION_MINOR;
    dmi->version[2] = DM_VERSION_PATCHLEVEL;
    dmi->data_size = sizeof(buf);
    dmi->data_start = sizeof(struct dm_ioctl);
    snprintf(dmi->name, sizeof(dmi->name), "%s", dm_name);
    if (ioctl(cfd, DM_DEV_CREATE, dmi) < 0 && errno != EBUSY) {
        int e = -errno;
        close(cfd);
        return e;
    }
    unsigned int dm_minor = minor(dmi->dev);

    /* 2. DM_TABLE_LOAD */
    memset(buf, 0, sizeof(buf));
    dmi->version[0] = DM_VERSION_MAJOR;
    dmi->version[1] = DM_VERSION_MINOR;
    dmi->version[2] = DM_VERSION_PATCHLEVEL;
    dmi->data_size = sizeof(buf);
    dmi->data_start = sizeof(struct dm_ioctl);
    dmi->flags = DM_READONLY_FLAG;
    dmi->target_count = num_exts;
    snprintf(dmi->name, sizeof(dmi->name), "%s", dm_name);

    size_t pos = sizeof(struct dm_ioctl);
    uint64_t logical_sec = 0;
    for (uint32_t i = 0; i < num_exts; i++) {
        struct dm_target_spec *ts = (struct dm_target_spec *)(buf + pos);
        char *params = (char *)(ts + 1);
        int plen;

        ts->sector_start = logical_sec;
        ts->length = exts[i].num_sectors;
        if (exts[i].target_type == 0) {
            snprintf(ts->target_type, sizeof(ts->target_type), "linear");
            plen = snprintf(params, 128, "%u:%u %llu",
                            major(super_rdev), minor(super_rdev),
                            (unsigned long long)exts[i].target_data) + 1;
        } else {
            snprintf(ts->target_type, sizeof(ts->target_type), "zero");
            params[0] = '\0';
            plen = 1;
        }
        size_t entry_sz = (sizeof(*ts) + plen + 7) & ~7UL;
        if (pos + entry_sz >= sizeof(buf)) {
            close(cfd);
            return -ENOSPC;
        }
        ts->next = (uint32_t)entry_sz;
        pos += entry_sz;
        logical_sec += exts[i].num_sectors;
    }

    if (ioctl(cfd, DM_TABLE_LOAD, dmi) < 0) {
        int e = -errno;
        close(cfd);
        return e;
    }

    /* 3. DM_DEV_SUSPEND (activates inactive table -> live!) */
    memset(buf, 0, sizeof(buf));
    dmi->version[0] = DM_VERSION_MAJOR;
    dmi->version[1] = DM_VERSION_MINOR;
    dmi->version[2] = DM_VERSION_PATCHLEVEL;
    dmi->data_size = sizeof(buf);
    dmi->data_start = sizeof(struct dm_ioctl);
    snprintf(dmi->name, sizeof(dmi->name), "%s", dm_name);
    if (ioctl(cfd, DM_DEV_SUSPEND, dmi) < 0) {
        int e = -errno;
        close(cfd);
        return e;
    }
    dm_minor = minor(dmi->dev);
    close(cfd);

    snprintf(dm_dev_out, out_sz, "/dev/dm-%u", dm_minor);
    if (access(dm_dev_out, F_OK) != 0)
        mknod(dm_dev_out, S_IFBLK | 0600, makedev(major(dmi->dev), dm_minor));
    return 0;
}

static int cmd_mount_vendor(const char *mnt_dir) {
    if (!mnt_dir || !mnt_dir[0]) mnt_dir = "/mnt/vendor";

    char super_dev[300];
    if (find_part_by_name("super", super_dev, sizeof(super_dev)) < 0) {
        fprintf(stderr, "super partition not found in /sys/block\n");
        return 1;
    }

    struct stat st;
    if (stat(super_dev, &st) < 0) {
        fprintf(stderr, "stat %s failed: %s\n", super_dev, strerror(errno));
        return 1;
    }

    int fd = open(super_dev, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", super_dev, strerror(errno));
        return 1;
    }

    struct lp_geometry geom;
    if (pread(fd, &geom, sizeof(geom), 4096) != (ssize_t)sizeof(geom) ||
        geom.magic != LP_GEOMETRY_MAGIC) {
        if (pread(fd, &geom, sizeof(geom), 8192) != (ssize_t)sizeof(geom) ||
            geom.magic != LP_GEOMETRY_MAGIC) {
            fprintf(stderr, "%s: invalid LP_METADATA_GEOMETRY\n", super_dev);
            close(fd);
            return 1;
        }
    }

    /* Check slots (prefer slot 1 for _b, fallback slot 0) */
    uint8_t *meta = malloc(geom.metadata_max_size);
    if (!meta) { close(fd); return 1; }

    const char *want_names[] = { "vendor_b", "vendor_a", "vendor" };
    struct lp_extent found_exts[64];
    uint32_t found_num_exts = 0;
    char found_name[40] = {0};

    uint32_t slots_order[4] = { 1, 0, 2, 3 };
    for (uint32_t si = 0; si < geom.metadata_slot_count && si < 4 && !found_num_exts; si++) {
        uint32_t slot = slots_order[si];
        if (slot >= geom.metadata_slot_count) continue;
        off_t off = 12288 + (off_t)slot * geom.metadata_max_size;
        if (pread(fd, meta, geom.metadata_max_size, off) != (ssize_t)geom.metadata_max_size)
            continue;
        struct lp_header *hdr = (struct lp_header *)meta;
        if (hdr->magic != LP_HEADER_MAGIC)
            continue;
        uint8_t *tables = meta + hdr->header_size;
        if (hdr->header_size + hdr->tables_size > geom.metadata_max_size)
            continue;

        for (int wn = 0; wn < 3 && !found_num_exts; wn++) {
            for (uint32_t p = 0; p < hdr->partitions.num_entries; p++) {
                struct lp_partition *part = (struct lp_partition *)
                    (tables + hdr->partitions.offset + p * hdr->partitions.entry_size);
                char pname[37];
                memcpy(pname, part->name, 36);
                pname[36] = '\0';
                if (strcmp(pname, want_names[wn]) == 0 && part->num_extents > 0) {
                    uint32_t ne = part->num_extents > 64 ? 64 : part->num_extents;
                    for (uint32_t e = 0; e < ne; e++) {
                        struct lp_extent *ex = (struct lp_extent *)
                            (tables + hdr->extents.offset +
                             (part->first_extent_index + e) * hdr->extents.entry_size);
                        found_exts[e] = *ex;
                    }
                    found_num_exts = ne;
                    snprintf(found_name, sizeof(found_name), "%s", pname);
                    break;
                }
            }
        }
    }
    free(meta);

    if (!found_num_exts) {
        fprintf(stderr, "vendor_b/vendor_a not found in %s LP metadata\n", super_dev);
        close(fd);
        return 1;
    }

    uint64_t total_sec = 0;
    for (uint32_t i = 0; i < found_num_exts; i++) total_sec += found_exts[i].num_sectors;
    printf("super=%s -> %s (%u extent(s), %.1f MiB)\n",
           super_dev, found_name, found_num_exts, (double)total_sec * 512.0 / 1048576.0);

    mkdir(mnt_dir, 0755);
    char blk_dev[64] = {0};

    /* Prefer Device-Mapper linear (handles 1 or N extents cleanly) */
    int rc = setup_dm_linear("np05j_vendor", st.st_rdev, found_exts, found_num_exts,
                             blk_dev, sizeof(blk_dev));
    if (rc < 0 && found_num_exts == 1 && found_exts[0].target_type == 0) {
        /* Fallback to loop0 for single-extent */
        int lfd = open("/dev/loop0", O_RDONLY);
        if (lfd < 0) {
            mknod("/dev/loop0", S_IFBLK | 0600, makedev(7, 0));
            lfd = open("/dev/loop0", O_RDONLY);
        }
        if (lfd >= 0) {
            struct loop_info64 li;
            memset(&li, 0, sizeof(li));
            li.lo_offset = found_exts[0].target_data * 512ULL;
            li.lo_sizelimit = found_exts[0].num_sectors * 512ULL;
            li.lo_flags = LO_FLAGS_READ_ONLY;
            if (ioctl(lfd, LOOP_SET_FD, fd) == 0 &&
                ioctl(lfd, LOOP_SET_STATUS64, &li) == 0) {
                snprintf(blk_dev, sizeof(blk_dev), "/dev/loop0");
                rc = 0;
            }
            close(lfd);
        }
    }
    close(fd);

    if (rc < 0 || !blk_dev[0]) {
        fprintf(stderr, "Failed to map %s (err=%d)\n", found_name, rc);
        return 1;
    }

    if (mount(blk_dev, mnt_dir, "erofs", MS_RDONLY, NULL) == 0 ||
        mount(blk_dev, mnt_dir, "ext4", MS_RDONLY, NULL) == 0) {
        printf("Mounted %s (%s) -> %s (ro)\n", blk_dev, found_name, mnt_dir);
        return 0;
    }
    fprintf(stderr, "mount %s -> %s failed: %s\n", blk_dev, mnt_dir, strerror(errno));
    return 1;
}

/* ==================== Adreno 830v2 DRM GPU Info ==================== */

struct drm_version_min {
    int version_major;
    int version_minor;
    int version_patchlevel;
    size_t name_len;
    char *name;
    size_t date_len;
    char *date;
    size_t desc_len;
    char *desc;
};

struct drm_msm_param_min {
    uint32_t pipe;
    uint32_t param;
    uint64_t value;
    uint32_t len;
    uint32_t pad;
};

#define DRM_IOCTL_VERSION_MIN  _IOWR('d', 0x00, struct drm_version_min)
#define DRM_IOCTL_MSM_GET_PARAM_MIN _IOWR('d', 0x40 + 0x00, struct drm_msm_param_min)

static uint64_t msm_get_param(int fd, uint32_t param, int *ok) {
    struct drm_msm_param_min p = { .pipe = 1 /* MSM_PIPE_3D0 */, .param = param };
    if (ioctl(fd, DRM_IOCTL_MSM_GET_PARAM_MIN, &p) == 0) {
        if (ok) *ok = 1;
        return p.value;
    }
    if (ok) *ok = 0;
    return 0;
}

static int cmd_gpu_info(const char *dev) {
    if (!dev || !dev[0]) dev = "/dev/dri/renderD128";
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", dev, strerror(errno));
        return 1;
    }
    char name[64] = {0}, date[64] = {0}, desc[128] = {0};
    struct drm_version_min ver = {
        .name_len = sizeof(name) - 1, .name = name,
        .date_len = sizeof(date) - 1, .date = date,
        .desc_len = sizeof(desc) - 1, .desc = desc,
    };
    if (ioctl(fd, DRM_IOCTL_VERSION_MIN, &ver) == 0) {
        printf("DRM %s: driver=%s v%d.%d.%d (%s) %s\n",
               dev, name, ver.version_major, ver.version_minor,
               ver.version_patchlevel, date, desc);
    }
    int ok = 0;
    uint64_t gpu_id = msm_get_param(fd, 0x01 /* GPU_ID */, &ok);
    uint64_t gmem   = msm_get_param(fd, 0x02 /* GMEM_SIZE */, &ok);
    uint64_t chipid = msm_get_param(fd, 0x03 /* CHIP_ID */, &ok);
    uint64_t maxf   = msm_get_param(fd, 0x04 /* MAX_FREQ */, &ok);
    uint64_t rings  = msm_get_param(fd, 0x07 /* NR_RINGS */, &ok);
    printf("Adreno GPU: gpu_id=%llu chip_id=0x%08llx GMEM=%llu MiB max_freq=%llu MHz rings=%llu\n",
           (unsigned long long)gpu_id,
           (unsigned long long)chipid,
           (unsigned long long)(gmem / (1024 * 1024)),
           (unsigned long long)(maxf / 1000000),
           (unsigned long long)rings);
    close(fd);
    return 0;
}

/* ==================== Legacy SPI TCM v2 Probe ==================== */

static uint8_t tcm2_crc6(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t seq) {
    uint8_t crc = 0x3f;
    uint8_t bytes[3] = {b0, b1, b2};
    for (int j = 0; j < 3; j++) {
        for (int i = 7; i >= 0; i--) {
            uint8_t bit = (bytes[j] >> i) & 1;
            uint8_t fb = ((crc >> 5) & 1) ^ bit;
            crc = (crc << 1) & 0x3f;
            if (fb) crc ^= 0x03;
        }
    }
    uint8_t tail[2] = {0, (uint8_t)(seq & 1)};
    for (int j = 0; j < 2; j++) {
        uint8_t fb = ((crc >> 5) & 1) ^ tail[j];
        crc = (crc << 1) & 0x3f;
        if (fb) crc ^= 0x03;
    }
    return (uint8_t)(((seq & 1) << 6) | crc);
}

static int spi_rw(int fd, uint32_t speed, const uint8_t *tx, uint8_t *rx, int len) {
    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (unsigned long)tx;
    tr.rx_buf = (unsigned long)rx;
    tr.len = len;
    tr.speed_hz = speed;
    tr.bits_per_word = 8;
    return ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
}

static int cmd_spi_probe(void) {
    int fd = open("/dev/spidev0.0", O_RDWR);
    if (fd < 0) {
        printf("spidev0.0 not present (bound to synaptics_tcm2_spi driver)\n");
        return 0;
    }
    uint8_t mode = 0, bits = 8;
    uint32_t speed = 1000000;
    ioctl(fd, SPI_IOC_WR_MODE, &mode);
    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
    uint8_t id_cmd[4] = {0x02, 0x00, 0x00, tcm2_crc6(0x02, 0x00, 0x00, 0)};
    uint8_t tx[64], rx[64];
    spi_rw(fd, speed, id_cmd, rx, 4);
    usleep(30000);
    memset(tx, 0xff, sizeof(tx));
    memset(rx, 0, sizeof(rx));
    if (spi_rw(fd, speed, tx, rx, 48) >= 0) {
        printf("TCM2 ID: code=0x%02x part='%.16s'\n", rx[0], (const char *)&rx[6]);
    }
    close(fd);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "bt-up") == 0)
        return cmd_bt_up((argc >= 3) ? atoi(argv[2]) : 0);
    if (argc >= 2 && strcmp(argv[1], "touch-watch") == 0)
        return cmd_touch_watch((argc >= 3) ? argv[2] : NULL);
    if (argc >= 2 && strcmp(argv[1], "wifi-scan") == 0)
        return cmd_wifi_scan((argc >= 3) ? argv[2] : "wlan0");
    if (argc >= 2 && strcmp(argv[1], "mount-vendor") == 0)
        return cmd_mount_vendor((argc >= 3) ? argv[2] : "/mnt/vendor");
    if (argc >= 2 && strcmp(argv[1], "gpu-info") == 0)
        return cmd_gpu_info((argc >= 3) ? argv[2] : "/dev/dri/renderD128");
    if (argc >= 2 && strcmp(argv[1], "spi-probe") == 0)
        return cmd_spi_probe();

    fprintf(stderr,
            "Usage: %s bt-up [hci] | touch-watch [event] | wifi-scan [wlan0] | "
            "mount-vendor [/mnt/vendor] | gpu-info [/dev/dri/renderD128]\n",
            argv[0]);
    return 1;
}
