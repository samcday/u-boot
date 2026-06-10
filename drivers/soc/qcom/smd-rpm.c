// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal Qualcomm RPM-SMD over GLINK-RPM transport.
 *
 * This implements the small synchronous subset needed by U-Boot regulator
 * clients on RPM GLINK systems.
 */

#define LOG_CATEGORY UCLASS_NOP

#include <asm/io.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <dm/ofnode.h>
#include <linux/bitops.h>
#include <linux/byteorder/little_endian.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <mailbox.h>
#include <malloc.h>
#include <soc/qcom/smd-rpm.h>
#include <time.h>

#define RPM_TOC_SIZE			256
#define RPM_TOC_MAGIC			0x67727430
#define RPM_TOC_MAX_ENTRIES		((RPM_TOC_SIZE - sizeof(struct rpm_toc)) / \
					 sizeof(struct rpm_toc_entry))

#define RPM_TX_FIFO_ID			0x61703272
#define RPM_RX_FIFO_ID			0x72326170

#define RPM_SERVICE_TYPE_REQUEST	0x00716572
#define RPM_MSG_TYPE_ERR		0x00727265
#define RPM_MSG_TYPE_MSG_ID		0x2367736d

#define RPM_REQUEST_TIMEOUT_US		5000000
#define GLINK_OPEN_TIMEOUT_US		5000000
#define GLINK_TX_TIMEOUT_US		100000
#define GLINK_VERSION_1			1
#define GLINK_NAME_SIZE			32
#define GLINK_MAX_TX			512
#define RPM_MAX_MSG			256

#define GLINK_CMD_VERSION		0
#define GLINK_CMD_VERSION_ACK		1
#define GLINK_CMD_OPEN			2
#define GLINK_CMD_CLOSE			3
#define GLINK_CMD_OPEN_ACK		4
#define GLINK_CMD_INTENT		5
#define GLINK_CMD_RX_DONE		6
#define GLINK_CMD_RX_INTENT_REQ		7
#define GLINK_CMD_RX_INTENT_REQ_ACK	8
#define GLINK_CMD_TX_DATA		9
#define GLINK_CMD_CLOSE_ACK		11
#define GLINK_CMD_TX_DATA_CONT		12
#define GLINK_CMD_READ_NOTIF		13
#define GLINK_CMD_RX_DONE_W_REUSE	14
#define GLINK_CMD_SIGNALS		15

struct rpm_toc_entry {
	__le32 id;
	__le32 offset;
	__le32 size;
} __packed;

struct rpm_toc {
	__le32 magic;
	__le32 count;
	struct rpm_toc_entry entries[];
} __packed;

struct glink_msg {
	__le16 cmd;
	__le16 param1;
	__le32 param2;
	u8 data[];
} __packed;

struct glink_tx_data {
	struct glink_msg msg;
	__le32 chunk_size;
	__le32 left_size;
} __packed;

struct qcom_rpm_header {
	__le32 service_type;
	__le32 length;
} __packed;

struct qcom_rpm_request {
	__le32 msg_id;
	__le32 flags;
	__le32 type;
	__le32 id;
	__le32 data_len;
} __packed;

struct qcom_rpm_message {
	__le32 msg_type;
	__le32 length;
	union {
		__le32 msg_id;
		u8 message[];
	};
} __packed;

struct glink_rpm_pipe {
	void __iomem *tail;
	void __iomem *head;
	void __iomem *fifo;
	size_t length;
};

struct qcom_glink_rpm {
	struct glink_rpm_pipe rx;
	struct glink_rpm_pipe tx;
	struct mbox_chan mbox;

	const char *channel_name;
	u16 lcid;
	u16 rcid;
	bool version_ack;
	bool open_ack;
	bool remote_open;

	u8 rx_buf[RPM_MAX_MSG];
	size_t rx_len;
	u32 wait_msg_id;
	bool ack_received;
	int ack_status;
	u32 msg_id;
};

struct qcom_smd_rpm {
	struct udevice *glink;
	u16 lcid;
	u16 rcid;
	bool open;
};

static size_t glink_rx_avail(struct glink_rpm_pipe *pipe)
{
	u32 head = readl(pipe->head);
	u32 tail = readl(pipe->tail);

	if (head < tail)
		return pipe->length - tail + head;

	return head - tail;
}

static size_t glink_tx_avail(struct glink_rpm_pipe *pipe)
{
	u32 head = readl(pipe->head);
	u32 tail = readl(pipe->tail);

	if (tail <= head)
		return pipe->length - head + tail;

	return tail - head;
}

static void glink_rx_peek(struct glink_rpm_pipe *pipe, void *data,
			  size_t offset, size_t count)
{
	u32 tail = readl(pipe->tail);
	size_t len;

	tail += offset;
	if (tail >= pipe->length)
		tail -= pipe->length;

	len = min(count, pipe->length - tail);
	if (len)
		memcpy_fromio(data, pipe->fifo + tail, len);
	if (len != count)
		memcpy_fromio(data + len, pipe->fifo, count - len);
}

static void glink_rx_advance(struct glink_rpm_pipe *pipe, size_t count)
{
	u32 tail = readl(pipe->tail);

	tail += count;
	if (tail >= pipe->length)
		tail -= pipe->length;

	writel(tail, pipe->tail);
}

static void glink_tx_write(struct glink_rpm_pipe *pipe, const void *data,
			   size_t count)
{
	u32 head = readl(pipe->head);
	size_t len;

	len = min(count, pipe->length - head);
	if (len)
		memcpy_toio(pipe->fifo + head, data, len);
	if (len != count)
		memcpy_toio(pipe->fifo, data + len, count - len);

	head += count;
	if (head >= pipe->length)
		head -= pipe->length;

	writel(head, pipe->head);
}

static int qcom_glink_tx(struct qcom_glink_rpm *rpm, const void *hdr,
			 size_t hlen, const void *data, size_t dlen)
{
	u8 buf[GLINK_MAX_TX] = { 0 };
	size_t len = hlen + dlen;
	size_t aligned = ALIGN(len, 8);
	ulong start;

	if (aligned > sizeof(buf))
		return -EINVAL;

	memcpy(buf, hdr, hlen);
	if (dlen)
		memcpy(buf + hlen, data, dlen);

	start = timer_get_us();
	while (glink_tx_avail(&rpm->tx) < aligned) {
		if (timer_get_us() - start > GLINK_TX_TIMEOUT_US)
			return -ETIMEDOUT;
		udelay(10);
	}

	glink_tx_write(&rpm->tx, buf, aligned);
	mbox_send(&rpm->mbox, NULL);

	return 0;
}

static int qcom_glink_send_version(struct qcom_glink_rpm *rpm)
{
	struct glink_msg msg = {
		.cmd = cpu_to_le16(GLINK_CMD_VERSION),
		.param1 = cpu_to_le16(GLINK_VERSION_1),
		.param2 = cpu_to_le32(0),
	};

	return qcom_glink_tx(rpm, &msg, sizeof(msg), NULL, 0);
}

static int qcom_glink_send_version_ack(struct qcom_glink_rpm *rpm)
{
	struct glink_msg msg = {
		.cmd = cpu_to_le16(GLINK_CMD_VERSION_ACK),
		.param1 = cpu_to_le16(GLINK_VERSION_1),
		.param2 = cpu_to_le32(0),
	};

	return qcom_glink_tx(rpm, &msg, sizeof(msg), NULL, 0);
}

static int qcom_glink_send_open_ack(struct qcom_glink_rpm *rpm)
{
	struct glink_msg msg = {
		.cmd = cpu_to_le16(GLINK_CMD_OPEN_ACK),
		.param1 = cpu_to_le16(rpm->rcid),
		.param2 = cpu_to_le32(0),
	};

	return qcom_glink_tx(rpm, &msg, sizeof(msg), NULL, 0);
}

static int qcom_glink_send_open(struct qcom_glink_rpm *rpm, const char *name)
{
	u8 buf[sizeof(struct glink_msg) + GLINK_NAME_SIZE] = { 0 };
	struct glink_msg *msg = (struct glink_msg *)buf;
	size_t name_len = strlen(name) + 1;
	size_t msg_len = sizeof(*msg) + name_len;

	if (name_len > GLINK_NAME_SIZE)
		return -EINVAL;

	rpm->lcid = 1;
	rpm->channel_name = name;
	rpm->open_ack = false;

	msg->cmd = cpu_to_le16(GLINK_CMD_OPEN);
	msg->param1 = cpu_to_le16(rpm->lcid);
	msg->param2 = cpu_to_le32(name_len);
	memcpy(msg->data, name, name_len);

	return qcom_glink_tx(rpm, msg, msg_len, NULL, 0);
}

static int qcom_glink_send_rx_intent_req_ack(struct qcom_glink_rpm *rpm,
					     u16 cid, bool granted)
{
	struct glink_msg msg = {
		.cmd = cpu_to_le16(GLINK_CMD_RX_INTENT_REQ_ACK),
		.param1 = cpu_to_le16(cid),
		.param2 = cpu_to_le32(granted),
	};

	return qcom_glink_tx(rpm, &msg, sizeof(msg), NULL, 0);
}

static int qcom_glink_send_data(struct qcom_glink_rpm *rpm, const void *data,
				size_t len)
{
	struct glink_tx_data hdr = {
		.msg = {
			.cmd = cpu_to_le16(GLINK_CMD_TX_DATA),
			.param1 = cpu_to_le16(rpm->lcid),
			.param2 = cpu_to_le32(0),
		},
		.chunk_size = cpu_to_le32(len),
		.left_size = cpu_to_le32(0),
	};

	return qcom_glink_tx(rpm, &hdr, sizeof(hdr), data, len);
}

static int qcom_rpm_parse_ack(struct qcom_glink_rpm *rpm, const void *data,
			      size_t count)
{
	const struct qcom_rpm_header *hdr = data;
	const struct qcom_rpm_message *msg;
	const u8 *buf = data + sizeof(*hdr);
	size_t hdr_length;
	const u8 *end;
	u32 msg_length;
	u32 msg_id;
	int status = 0;
	bool found = false;

	if (count < sizeof(*hdr))
		return -EINVAL;

	if (le32_to_cpu(hdr->service_type) != RPM_SERVICE_TYPE_REQUEST)
		return -EINVAL;

	hdr_length = le32_to_cpu(hdr->length);
	if (hdr_length > count - sizeof(*hdr))
		return -EINVAL;

	end = buf + hdr_length;
	while (buf + sizeof(*msg) <= end) {
		msg = (const struct qcom_rpm_message *)buf;
		msg_length = le32_to_cpu(msg->length);

		if (buf + sizeof(__le32) * 2 + msg_length > end)
			return -EINVAL;

		switch (le32_to_cpu(msg->msg_type)) {
		case RPM_MSG_TYPE_MSG_ID:
			if (msg_length >= sizeof(__le32)) {
				msg_id = le32_to_cpu(msg->msg_id);
				if (msg_id == rpm->wait_msg_id)
					found = true;
			}
			break;
		case RPM_MSG_TYPE_ERR:
			status = -EINVAL;
			break;
		default:
			break;
		}

		buf = PTR_ALIGN(buf + sizeof(__le32) * 2 + msg_length, 4);
	}

	if (!found)
		return 0;

	rpm->ack_status = status;
	rpm->ack_received = true;

	return 0;
}

static int qcom_glink_rx_data(struct qcom_glink_rpm *rpm, size_t avail)
{
	struct glink_tx_data hdr;
	u32 chunk_size;
	u32 left_size;
	u16 lcid;
	size_t msg_len;

	if (avail < sizeof(hdr))
		return -EAGAIN;

	glink_rx_peek(&rpm->rx, &hdr, 0, sizeof(hdr));
	chunk_size = le32_to_cpu(hdr.chunk_size);
	left_size = le32_to_cpu(hdr.left_size);
	lcid = le16_to_cpu(hdr.msg.param1);
	msg_len = sizeof(hdr) + chunk_size;

	if (avail < msg_len)
		return -EAGAIN;

	if (lcid == rpm->lcid && rpm->rx_len + chunk_size <= sizeof(rpm->rx_buf)) {
		glink_rx_peek(&rpm->rx, rpm->rx_buf + rpm->rx_len,
			      sizeof(hdr), chunk_size);
		rpm->rx_len += chunk_size;

		if (!left_size) {
			qcom_rpm_parse_ack(rpm, rpm->rx_buf, rpm->rx_len);
			rpm->rx_len = 0;
		}
	} else {
		rpm->rx_len = 0;
	}

	glink_rx_advance(&rpm->rx, ALIGN(msg_len, 8));

	return 0;
}

static int qcom_glink_poll_once(struct qcom_glink_rpm *rpm)
{
	struct glink_msg msg;
	size_t avail = glink_rx_avail(&rpm->rx);
	u32 param2;
	u16 cmd;
	u16 param1;
	size_t extra;
	u8 name[GLINK_NAME_SIZE];

	if (avail < sizeof(msg))
		return -EAGAIN;

	glink_rx_peek(&rpm->rx, &msg, 0, sizeof(msg));
	cmd = le16_to_cpu(msg.cmd);
	param1 = le16_to_cpu(msg.param1);
	param2 = le32_to_cpu(msg.param2);

	switch (cmd) {
	case GLINK_CMD_VERSION:
		glink_rx_advance(&rpm->rx, ALIGN(sizeof(msg), 8));
		return qcom_glink_send_version_ack(rpm);
	case GLINK_CMD_VERSION_ACK:
		rpm->version_ack = param1 == GLINK_VERSION_1;
		glink_rx_advance(&rpm->rx, ALIGN(sizeof(msg), 8));
		return 0;
	case GLINK_CMD_OPEN:
		extra = param2 & 0xffff;
		if (extra > sizeof(name))
			return -EINVAL;
		if (avail < sizeof(msg) + extra)
			return -EAGAIN;
		memset(name, 0, sizeof(name));
		glink_rx_peek(&rpm->rx, name, sizeof(msg), extra);
		if (rpm->channel_name && !strcmp((const char *)name, rpm->channel_name)) {
			rpm->rcid = param1;
			rpm->remote_open = true;
		}
		glink_rx_advance(&rpm->rx, sizeof(msg) + ALIGN(extra, 8));
		return 0;
	case GLINK_CMD_OPEN_ACK:
		if (param1 == rpm->lcid)
			rpm->open_ack = true;
		glink_rx_advance(&rpm->rx, ALIGN(sizeof(msg), 8));
		return 0;
	case GLINK_CMD_TX_DATA:
	case GLINK_CMD_TX_DATA_CONT:
		return qcom_glink_rx_data(rpm, avail);
	case GLINK_CMD_RX_INTENT_REQ:
		glink_rx_advance(&rpm->rx, ALIGN(sizeof(msg), 8));
		return qcom_glink_send_rx_intent_req_ack(rpm, param1, false);
	case GLINK_CMD_READ_NOTIF:
		glink_rx_advance(&rpm->rx, ALIGN(sizeof(msg), 8));
		mbox_send(&rpm->mbox, NULL);
		return 0;
	case GLINK_CMD_CLOSE:
	case GLINK_CMD_CLOSE_ACK:
	case GLINK_CMD_INTENT:
	case GLINK_CMD_RX_DONE:
	case GLINK_CMD_RX_DONE_W_REUSE:
	case GLINK_CMD_SIGNALS:
	default:
		glink_rx_advance(&rpm->rx, ALIGN(sizeof(msg), 8));
		return 0;
	}
}

static int qcom_glink_poll_until(struct qcom_glink_rpm *rpm,
				 bool (*done)(struct qcom_glink_rpm *rpm),
				 ulong timeout_us)
{
	ulong start = timer_get_us();
	int ret;

	while (!done(rpm)) {
		ret = qcom_glink_poll_once(rpm);
		if (ret && ret != -EAGAIN)
			return ret;
		if (timer_get_us() - start > timeout_us)
			return -ETIMEDOUT;
		if (ret == -EAGAIN)
			udelay(50);
	}

	return 0;
}

static bool qcom_glink_version_done(struct qcom_glink_rpm *rpm)
{
	return rpm->version_ack;
}

static bool qcom_glink_open_done(struct qcom_glink_rpm *rpm)
{
	return rpm->open_ack && rpm->remote_open;
}

static bool qcom_rpm_ack_done(struct qcom_glink_rpm *rpm)
{
	return rpm->ack_received;
}

static int qcom_glink_open(struct udevice *dev, const char *name,
			   u16 *lcid, u16 *rcid)
{
	struct qcom_glink_rpm *rpm = dev_get_priv(dev);
	int ret;

	if (rpm->open_ack && rpm->remote_open) {
		*lcid = rpm->lcid;
		*rcid = rpm->rcid;
		return 0;
	}

	rpm->channel_name = name;

	if (!rpm->version_ack) {
		ret = qcom_glink_poll_until(rpm, qcom_glink_version_done,
					    GLINK_OPEN_TIMEOUT_US);
		if (ret)
			return ret;
	}

	ret = qcom_glink_send_open(rpm, name);
	if (ret)
		return ret;

	ret = qcom_glink_poll_until(rpm, qcom_glink_open_done,
				    GLINK_OPEN_TIMEOUT_US);
	if (ret)
		return ret;

	ret = qcom_glink_send_open_ack(rpm);
	if (ret)
		return ret;

	*lcid = rpm->lcid;
	*rcid = rpm->rcid;

	return 0;
}

int qcom_rpm_smd_write(struct udevice *dev, int state, u32 resource_type,
		       u32 resource_id, const void *buf, size_t count)
{
	struct qcom_smd_rpm *smd = dev_get_priv(dev);
	struct qcom_glink_rpm *rpm = dev_get_priv(smd->glink);
	u8 pkt[sizeof(struct qcom_rpm_header) + sizeof(struct qcom_rpm_request) +
	       RPM_MAX_MSG] = { 0 };
	struct qcom_rpm_header *hdr = (struct qcom_rpm_header *)pkt;
	struct qcom_rpm_request *req = (struct qcom_rpm_request *)(hdr + 1);
	size_t size = sizeof(*hdr) + sizeof(*req) + count;
	u32 msg_id;
	int ret;

	if (!smd->open)
		return -ENODEV;
	if (count > RPM_MAX_MSG || size >= RPM_MAX_MSG)
		return -EINVAL;

	msg_id = ++rpm->msg_id;
	if (!msg_id)
		msg_id = ++rpm->msg_id;

	hdr->service_type = cpu_to_le32(RPM_SERVICE_TYPE_REQUEST);
	hdr->length = cpu_to_le32(sizeof(*req) + count);

	req->msg_id = cpu_to_le32(msg_id);
	req->flags = cpu_to_le32(state);
	req->type = cpu_to_le32(resource_type);
	req->id = cpu_to_le32(resource_id);
	req->data_len = cpu_to_le32(count);
	memcpy(req + 1, buf, count);

	rpm->wait_msg_id = msg_id;
	rpm->ack_received = false;
	rpm->ack_status = 0;

	ret = qcom_glink_send_data(rpm, pkt, size);
	if (ret)
		return ret;

	ret = qcom_glink_poll_until(rpm, qcom_rpm_ack_done,
				    RPM_REQUEST_TIMEOUT_US);
	if (ret)
		return ret;

	return rpm->ack_status;
}

static int glink_rpm_parse_toc(struct udevice *dev, void __iomem *msg_ram,
			       size_t msg_ram_size, struct glink_rpm_pipe *rx,
			       struct glink_rpm_pipe *tx)
{
	u8 buf[RPM_TOC_SIZE];
	struct rpm_toc *toc = (struct rpm_toc *)buf;
	u32 num_entries;
	int i;

	memcpy_fromio(buf, msg_ram + msg_ram_size - RPM_TOC_SIZE,
		      sizeof(buf));

	if (le32_to_cpu(toc->magic) != RPM_TOC_MAGIC) {
		dev_err(dev, "RPM TOC has invalid magic\n");
		return -EINVAL;
	}

	num_entries = le32_to_cpu(toc->count);
	if (num_entries > RPM_TOC_MAX_ENTRIES)
		return -EINVAL;

	for (i = 0; i < num_entries; i++) {
		u32 id = le32_to_cpu(toc->entries[i].id);
		u32 offset = le32_to_cpu(toc->entries[i].offset);
		u32 size = le32_to_cpu(toc->entries[i].size);

		if (offset > msg_ram_size || offset + size > msg_ram_size)
			continue;

		switch (id) {
		case RPM_RX_FIFO_ID:
			rx->length = size;
			rx->tail = msg_ram + offset;
			rx->head = msg_ram + offset + sizeof(u32);
			rx->fifo = msg_ram + offset + sizeof(u32) * 2;
			break;
		case RPM_TX_FIFO_ID:
			tx->length = size;
			tx->tail = msg_ram + offset;
			tx->head = msg_ram + offset + sizeof(u32);
			tx->fifo = msg_ram + offset + sizeof(u32) * 2;
			break;
		default:
			break;
		}
	}

	if (!rx->fifo || !tx->fifo)
		return -EINVAL;

	return 0;
}

static int qcom_glink_rpm_probe(struct udevice *dev)
{
	struct qcom_glink_rpm *rpm = dev_get_priv(dev);
	struct resource res;
	ofnode msg_ram_node;
	void __iomem *msg_ram;
	int ret;

	msg_ram_node = ofnode_parse_phandle(dev_ofnode(dev), "qcom,rpm-msg-ram", 0);
	if (!ofnode_valid(msg_ram_node))
		return -EINVAL;

	ret = ofnode_read_resource(msg_ram_node, 0, &res);
	if (ret)
		return ret;

	msg_ram = devm_ioremap(dev, res.start, resource_size(&res));
	if (!msg_ram)
		return -ENOMEM;

	ret = glink_rpm_parse_toc(dev, msg_ram, resource_size(&res),
				  &rpm->rx, &rpm->tx);
	if (ret)
		return ret;

	ret = mbox_get_by_index(dev, 0, &rpm->mbox);
	if (ret) {
		dev_err(dev, "failed to acquire IPC channel: %d\n", ret);
		return ret;
	}

	writel(0, rpm->tx.head);
	writel(0, rpm->rx.tail);

	ret = qcom_glink_send_version(rpm);
	if (ret)
		return ret;

	return 0;
}

static int qcom_smd_rpm_probe(struct udevice *dev)
{
	struct qcom_smd_rpm *smd = dev_get_priv(dev);
	const char *name;
	int ret;

	name = dev_read_string(dev, "qcom,glink-channels");
	if (!name)
		name = "rpm_requests";

	smd->glink = dev->parent;

	ret = qcom_glink_open(smd->glink, name, &smd->lcid, &smd->rcid);
	if (ret) {
		dev_err(dev, "failed to open GLINK channel %s: %d\n",
			name, ret);
		return ret;
	}

	smd->open = true;

	return 0;
}

static const struct udevice_id qcom_glink_rpm_of_match[] = {
	{ .compatible = "qcom,glink-rpm" },
	{ }
};

U_BOOT_DRIVER(qcom_glink_rpm) = {
	.name = "qcom-glink-rpm",
	.id = UCLASS_NOP,
	.of_match = qcom_glink_rpm_of_match,
	.bind = dm_scan_fdt_dev,
	.probe = qcom_glink_rpm_probe,
	.priv_auto = sizeof(struct qcom_glink_rpm),
	.flags = DM_FLAG_DEFAULT_PD_CTRL_OFF,
};

static const struct udevice_id qcom_smd_rpm_of_match[] = {
	{ .compatible = "qcom,glink-smd-rpm" },
	{ .compatible = "qcom,rpm-sm6125" },
	{ .compatible = "qcom,rpm-sm6115" },
	{ }
};

U_BOOT_DRIVER(qcom_smd_rpm) = {
	.name = "qcom-smd-rpm",
	.id = UCLASS_NOP,
	.of_match = qcom_smd_rpm_of_match,
	.bind = dm_scan_fdt_dev,
	.probe = qcom_smd_rpm_probe,
	.priv_auto = sizeof(struct qcom_smd_rpm),
	.flags = DM_FLAG_DEFAULT_PD_CTRL_OFF,
};
