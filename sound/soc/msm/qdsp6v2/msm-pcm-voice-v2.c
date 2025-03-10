/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/control.h>
#include <asm/dma.h>
#include <linux/of_device.h>

#include "msm-pcm-voice-v2.h"
#include "msm-qti-pp-config.h"
#include "q6voice.h"

struct msm_dtmf_detected_event_data {
	uint32_t event_type;
	uint32_t payload_len;
	struct vss_istream_evt_rx_dtmf_detected dtmf_payload;
};

static struct msm_voice voice_info[VOICE_SESSION_INDEX_MAX];

static struct snd_pcm_hardware msm_pcm_hardware = {

	.info =                 (SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_PAUSE |
				SNDRV_PCM_INFO_RESUME),
	.formats =              SNDRV_PCM_FMTBIT_S16_LE,
	.rates =                SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000,
	.rate_min =             8000,
	.rate_max =             16000,
	.channels_min =         1,
	.channels_max =         1,

	.buffer_bytes_max =     4096 * 2,
	.period_bytes_min =     2048,
	.period_bytes_max =     4096,
	.periods_min =          2,
	.periods_max =          4,

	.fifo_size =            0,
};

static int is_valid_session_id(uint32_t session_id)
{
	int idx = 0;

	switch (session_id) {
	case VOICE_SESSION_VSID:
	case VOICE2_SESSION_VSID:
	case VOLTE_SESSION_VSID:
	case QCHAT_SESSION_VSID:
	case VOWLAN_SESSION_VSID:
	case VOICEMMODE1_VSID:
	case VOICEMMODE2_VSID:
	case ALL_SESSION_VSID:
		idx = 1;
		break;
	default:
		pr_debug("%s: Invalid session_id : %x\n", __func__, session_id);
		break;
	}
	return idx;
}

static int get_idx_for_session(uint32_t session_id)
{
	int idx = 0;

	switch (session_id) {
	case VOICE_SESSION_VSID:
		idx = VOICE_SESSION_INDEX;
		break;
	case VOICE2_SESSION_VSID:
		idx = VOICE2_SESSION_INDEX;
		break;
	case VOLTE_SESSION_VSID:
		idx = VOLTE_SESSION_INDEX;
		break;
	case QCHAT_SESSION_VSID:
		idx = QCHAT_SESSION_INDEX;
		break;
	case VOWLAN_SESSION_VSID:
		idx = VOWLAN_SESSION_INDEX;
		break;
	case VOICEMMODE1_VSID:
		idx = VOICEMMODE1_INDEX;
		break;
	case VOICEMMODE2_VSID:
		idx = VOICEMMODE2_INDEX;
		break;
	case ALL_SESSION_VSID:
		idx = VOICE_SESSION_INDEX_MAX - 1;
		break;
	default:
		pr_err("%s: Invalid session_id : %x\n", __func__, session_id);
		break;
	}

	return idx;
}

static bool is_volte(struct msm_voice *pvolte)
{
	if (pvolte == &voice_info[VOLTE_SESSION_INDEX])
		return true;
	else
		return false;
}

static bool is_voice2(struct msm_voice *pvoice2)
{
	if (pvoice2 == &voice_info[VOICE2_SESSION_INDEX])
		return true;
	else
		return false;
}

static bool is_qchat(struct msm_voice *pqchat)
{
	if (pqchat == &voice_info[QCHAT_SESSION_INDEX])
		return true;
	else
		return false;
}

static bool is_vowlan(struct msm_voice *pvowlan)
{
	if (pvowlan == &voice_info[VOWLAN_SESSION_INDEX])
		return true;
	else
		return false;
}

static bool is_voicemmode1(struct msm_voice *pvoicemmode1)
{
	if (pvoicemmode1 == &voice_info[VOICEMMODE1_INDEX])
		return true;
	else
		return false;
}

static bool is_voicemmode2(struct msm_voice *pvoicemmode2)
{
	if (pvoicemmode2 == &voice_info[VOICEMMODE2_INDEX])
		return true;
	else
		return false;
}

static uint32_t get_session_id(struct msm_voice *pvoc)
{
	uint32_t session_id = 0;

	if (is_volte(pvoc))
		session_id = voc_get_session_id(VOLTE_SESSION_NAME);
	else if (is_voice2(pvoc))
		session_id = voc_get_session_id(VOICE2_SESSION_NAME);
	else if (is_qchat(pvoc))
		session_id = voc_get_session_id(QCHAT_SESSION_NAME);
	else if (is_vowlan(pvoc))
		session_id = voc_get_session_id(VOWLAN_SESSION_NAME);
	else if (is_voicemmode1(pvoc))
		session_id = voc_get_session_id(VOICEMMODE1_NAME);
	else if (is_voicemmode2(pvoc))
		session_id = voc_get_session_id(VOICEMMODE2_NAME);
	else
		session_id = voc_get_session_id(VOICE_SESSION_NAME);

	return session_id;
}

static void dtmf_rx_detected_evt_hdlr(uint8_t *pkt,
				char *session,
				void *private_data)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *soc_prtd = private_data;
	struct msm_dtmf_detected_event_data event_data = {ADSP_STREAM_PP_EVENT,
		sizeof(struct vss_istream_evt_rx_dtmf_detected),
		{0} };

	if (!pkt) {
		pr_err("%s: packet is NULL\n",
			__func__);
		return;
	}

	if (!private_data) {
		pr_err("%s: private_data is NULL\n",
			__func__);
		return;
	}

	memcpy(&event_data.dtmf_payload, pkt,
		sizeof(struct vss_istream_evt_rx_dtmf_detected));

	ret = msm_adsp_inform_mixer_ctl(soc_prtd, (uint32_t *)&event_data);
	if (ret) {
		pr_err("%s: failed to inform mixer ctl. err = %d\n",
			__func__, ret);
		return;
	}
}

static int msm_pcm_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_voice *prtd = runtime->private_data;

	pr_debug("%s\n", __func__);

	if (!prtd->playback_start)
		prtd->playback_start = 1;

	return 0;
}

static int msm_pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_voice *prtd = runtime->private_data;

	pr_debug("%s\n", __func__);

	if (!prtd->capture_start)
		prtd->capture_start = 1;

	return 0;
}
static int msm_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_prtd = substream->private_data;
	struct msm_voice *voice;

	if (!strncmp("VoLTE", substream->pcm->id, 5)) {
		voice = &voice_info[VOLTE_SESSION_INDEX];
		pr_debug("%s: Open VoLTE Substream Id=%s\n",
			 __func__, substream->pcm->id);
	} else if (!strncmp("Voice2", substream->pcm->id, 6)) {
		voice = &voice_info[VOICE2_SESSION_INDEX];
		pr_debug("%s: Open Voice2 Substream Id=%s\n",
			 __func__, substream->pcm->id);
	} else if (!strncmp("QCHAT", substream->pcm->id, 5)) {
		voice = &voice_info[QCHAT_SESSION_INDEX];
		pr_debug("%s: Open QCHAT Substream Id=%s\n",
			 __func__, substream->pcm->id);
	} else if (!strncmp("VoWLAN", substream->pcm->id, 6)) {
		voice = &voice_info[VOWLAN_SESSION_INDEX];
		pr_debug("%s: Open VoWLAN Substream Id=%s\n",
			 __func__, substream->pcm->id);
	} else if (!strncmp("VoiceMMode1", substream->pcm->id, 11)) {
		voice = &voice_info[VOICEMMODE1_INDEX];
		pr_debug("%s: Open VoiceMMode1 Substream Id=%s\n",
			 __func__, substream->pcm->id);
	} else if (!strncmp("VoiceMMode2", substream->pcm->id, 11)) {
		voice = &voice_info[VOICEMMODE2_INDEX];
		pr_debug("%s: Open VoiceMMode2 Substream Id=%s\n",
			 __func__, substream->pcm->id);
	} else {
		voice = &voice_info[VOICE_SESSION_INDEX];
		pr_debug("%s: Open VOICE Substream Id=%s\n",
			 __func__, substream->pcm->id);
	}
	mutex_lock(&voice->lock);

	runtime->hw = msm_pcm_hardware;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		voice->playback_substream = substream;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		voice->capture_substream = substream;

	voice->instance++;
	pr_debug("%s: Instance = %d, Stream ID = %s\n",
			__func__ , voice->instance, substream->pcm->id);
	runtime->private_data = voice;

	mutex_unlock(&voice->lock);
	msm_adsp_init_mixer_ctl_pp_event_queue(soc_prtd);
	return 0;
}
static int msm_pcm_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_voice *prtd = runtime->private_data;

	pr_debug("%s\n", __func__);

	if (prtd->playback_start)
		prtd->playback_start = 0;

	prtd->playback_substream = NULL;

	return 0;
}
static int msm_pcm_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_voice *prtd = runtime->private_data;

	pr_debug("%s\n", __func__);

	if (prtd->capture_start)
		prtd->capture_start = 0;
	prtd->capture_substream = NULL;

	return 0;
}
static int msm_pcm_close(struct snd_pcm_substream *substream)
{

	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *soc_prtd = substream->private_data;
	struct msm_voice *prtd = runtime->private_data;
	uint32_t session_id = 0;
	int ret = 0;

	mutex_lock(&prtd->lock);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		ret = msm_pcm_playback_close(substream);
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		ret = msm_pcm_capture_close(substream);

	prtd->instance--;
	if (!prtd->playback_start && !prtd->capture_start) {
		pr_debug("end voice call\n");

		session_id = get_session_id(prtd);
		if (session_id)
			voc_end_voice_call(session_id);
	}
	mutex_unlock(&prtd->lock);
	msm_adsp_clean_mixer_ctl_pp_event_queue(soc_prtd);

	return ret;
}
static int msm_pcm_prepare(struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_voice *prtd = runtime->private_data;
	uint32_t session_id = 0;

	mutex_lock(&prtd->lock);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		ret = msm_pcm_playback_prepare(substream);
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		ret = msm_pcm_capture_prepare(substream);

	if (prtd->playback_start && prtd->capture_start) {
		session_id = get_session_id(prtd);
		if (session_id)
			voc_start_voice_call(session_id);
	}
	mutex_unlock(&prtd->lock);

	return ret;
}

static int msm_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{

	pr_debug("%s: Voice\n", __func__);

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);

	return 0;
}

static int msm_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_voice *prtd = runtime->private_data;
	uint32_t session_id = 0;

	pr_debug("%s: cmd = %d\n", __func__, cmd);

	session_id = get_session_id(prtd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_STOP:
		pr_debug("Start & Stop Voice call not handled in Trigger.\n");
	break;
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		pr_debug("%s: resume call session_id = %d\n", __func__,
			 session_id);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			ret = msm_pcm_playback_prepare(substream);
		else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			ret = msm_pcm_capture_prepare(substream);
		if (prtd->playback_start && prtd->capture_start) {
			if (session_id)
				voc_resume_voice_call(session_id);
		}
	break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		pr_debug("%s: pause call session_id=%d\n",
			 __func__, session_id);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			if (prtd->playback_start)
				prtd->playback_start = 0;
		} else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
			if (prtd->capture_start)
				prtd->capture_start = 0;
		}
		if (session_id)
			voc_standby_voice_call(session_id);
		break;
	default:
		ret = -EINVAL;
	break;
	}
	return ret;
}

static int msm_pcm_ioctl(struct snd_pcm_substream *substream,
			 unsigned int cmd, void *arg)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct msm_voice *prtd = runtime->private_data;
	uint32_t session_id = get_session_id(prtd);
	enum voice_lch_mode lch_mode;
	int ret = 0;

	switch (cmd) {
	case SNDRV_VOICE_IOCTL_LCH:
		if (copy_from_user(&lch_mode, (void *)arg,
				   sizeof(enum voice_lch_mode))) {
			pr_err("%s: Copy from user failed, size %zd\n",
				__func__, sizeof(enum voice_lch_mode));

			ret = -EFAULT;
			break;
		}

		pr_debug("%s: %s lch_mode:%d\n",
			 __func__, substream->pcm->id, lch_mode);

		switch (lch_mode) {
		case VOICE_LCH_START:
		case VOICE_LCH_STOP:
			ret = voc_set_lch(session_id, lch_mode);
			break;

		default:
			pr_err("%s: Invalid LCH MODE %d\n", __func__, lch_mode);

			ret = -EFAULT;
		}

		break;
	default:
		pr_debug("%s: Falling into default snd_lib_ioctl cmd 0x%x\n",
			 __func__, cmd);

		ret = snd_pcm_lib_ioctl(substream, cmd, arg);
		break;
	}

	if (!ret)
		pr_debug("%s: ret %d\n", __func__, ret);
	else
		pr_err("%s: cmd 0x%x failed %d\n", __func__, cmd, ret);

	return ret;
}

static int msm_voice_sidetone_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	int ret;
	bool sidetone_enable = ucontrol->value.integer.value[0];
	uint32_t session_id = ALL_SESSION_VSID;

	ret = voc_set_afe_sidetone(session_id, sidetone_enable);
	pr_debug("%s: AFE Sidetone enable=%d session_id=0x%x ret=%d\n",
		 __func__, sidetone_enable, session_id, ret);
	return ret;
}

static int msm_voice_sidetone_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{

	ucontrol->value.integer.value[0] = voc_get_afe_sidetone();
	return 0;
}

static int msm_voice_gain_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	int volume = ucontrol->value.integer.value[0];
	uint32_t session_id = ucontrol->value.integer.value[1];
	int ramp_duration = ucontrol->value.integer.value[2];

	if ((volume < 0) || (ramp_duration < 0)
		|| (ramp_duration > MAX_RAMP_DURATION)) {
		pr_err(" %s Invalid arguments", __func__);

		ret = -EINVAL;
		goto done;
	}

	pr_debug("%s: volume: %d session_id: %#x ramp_duration: %d\n", __func__,
		volume, session_id, ramp_duration);

	voc_set_rx_vol_step(session_id, RX_PATH, volume, ramp_duration);

done:
	return ret;
}

static int msm_voice_mute_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	int mute = ucontrol->value.integer.value[0];
	uint32_t session_id = ucontrol->value.integer.value[1];
	int ramp_duration = ucontrol->value.integer.value[2];

	if ((mute < 0) || (mute > 1) || (ramp_duration < 0)
		|| (ramp_duration > MAX_RAMP_DURATION)) {
		pr_err(" %s Invalid arguments", __func__);

		ret = -EINVAL;
		goto done;
	}

	pr_debug("%s: mute=%d session_id=%#x ramp_duration=%d\n", __func__,
		mute, session_id, ramp_duration);

	ret = voc_set_tx_mute(session_id, TX_PATH, mute, ramp_duration);

done:
	return ret;
}

static int msm_voice_tx_device_mute_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	int mute = ucontrol->value.integer.value[0];
	uint32_t session_id = ucontrol->value.integer.value[1];
	int ramp_duration = ucontrol->value.integer.value[2];

	if ((mute < 0) || (mute > 1) || (ramp_duration < 0) ||
	    (ramp_duration > MAX_RAMP_DURATION)) {
		pr_err(" %s Invalid arguments", __func__);

		ret = -EINVAL;
		goto done;
	}

	pr_debug("%s: mute=%d session_id=%#x ramp_duration=%d\n", __func__,
		 mute, session_id, ramp_duration);

	ret = voc_set_device_mute(session_id, VSS_IVOLUME_DIRECTION_TX,
				  mute, ramp_duration);

done:
	return ret;
}

static int msm_voice_rx_device_mute_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	int mute = ucontrol->value.integer.value[0];
	uint32_t session_id = ucontrol->value.integer.value[1];
	int ramp_duration = ucontrol->value.integer.value[2];

	if ((mute < 0) || (mute > 1) || (ramp_duration < 0) ||
	    (ramp_duration > MAX_RAMP_DURATION)) {
		pr_err(" %s Invalid arguments", __func__);

		ret = -EINVAL;
		goto done;
	}

	pr_debug("%s: mute=%d session_id=%#x ramp_duration=%d\n", __func__,
		 mute, session_id, ramp_duration);

	voc_set_device_mute(session_id, VSS_IVOLUME_DIRECTION_RX,
			    mute, ramp_duration);

done:
	return ret;
}



static const char *tty_mode[] = {"OFF", "HCO", "VCO", "FULL"};
static const struct soc_enum msm_tty_mode_enum[] = {
		SOC_ENUM_SINGLE_EXT(4, tty_mode),
};

static int msm_voice_tty_mode_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] =
		voc_get_tty_mode(voc_get_session_id(VOICE_SESSION_NAME));
	return 0;
}

static int msm_voice_tty_mode_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	int tty_mode = ucontrol->value.integer.value[0];

	pr_debug("%s: tty_mode=%d\n", __func__, tty_mode);

	voc_set_tty_mode(voc_get_session_id(VOICE_SESSION_NAME), tty_mode);
	voc_set_tty_mode(voc_get_session_id(VOICE2_SESSION_NAME), tty_mode);
	voc_set_tty_mode(voc_get_session_id(VOLTE_SESSION_NAME), tty_mode);
	voc_set_tty_mode(voc_get_session_id(VOWLAN_SESSION_NAME), tty_mode);
	voc_set_tty_mode(voc_get_session_id(VOICEMMODE1_NAME), tty_mode);
	voc_set_tty_mode(voc_get_session_id(VOICEMMODE2_NAME), tty_mode);

	return 0;
}

static int msm_voice_slowtalk_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	int st_enable = ucontrol->value.integer.value[0];
	uint32_t session_id = ucontrol->value.integer.value[1];
	struct module_instance_info mod_inst_info = {0};

	pr_debug("%s: st enable=%d session_id=%#x\n", __func__, st_enable,
		 session_id);

	mod_inst_info.module_id = MODULE_ID_VOICE_MODULE_ST;
	mod_inst_info.instance_id = INSTANCE_ID_0;
	voc_set_pp_enable(session_id, mod_inst_info, st_enable);

	return 0;
}

static int msm_voice_hd_voice_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	uint32_t hd_enable = ucontrol->value.integer.value[0];
	uint32_t session_id = ucontrol->value.integer.value[1];

	pr_debug("%s: HD Voice enable=%d session_id=%#x\n", __func__, hd_enable,
		 session_id);

	ret = voc_set_hd_enable(session_id, hd_enable);

	return ret;
}

static int msm_dtmf_detect_rx_vsid_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	uint32_t session_id = ucontrol->value.integer.value[0];
	uint32_t enable = ucontrol->value.integer.value[1];

	if (enable)
		enable = 1;

	pr_debug("%s: sess_id=%d enable=%d\n", __func__, session_id, enable);

	return voc_enable_dtmf_rx_detection(session_id, enable);
}

static int msm_dtmf_detect_rx_vsid_cb_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct msm_voice *dtmf_voice_info = NULL;
	uint32_t session_id = ucontrol->value.integer.value[0];
	uint32_t enable = ucontrol->value.integer.value[1];

	if (!is_valid_session_id(session_id)) {
		pr_err(" %s Invalid session_id : %x\n", __func__, session_id);
		return -EINVAL;
	}

	if (enable)
		enable = 1;

	pr_debug("%s: enable dtmf detect cb =%d for session_id=%d\n",
		__func__, enable, session_id);

	dtmf_voice_info = &voice_info[get_idx_for_session(session_id)];
	voc_register_dtmf_rx_detection_cb
		((dtmf_rx_det_cb_fn) dtmf_rx_detected_evt_hdlr,
		(void *) dtmf_voice_info->capture_substream->private_data);

	return 0;
}

static int msm_voice_topology_disable_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	int disable = ucontrol->value.integer.value[0];
	uint32_t session_id = ucontrol->value.integer.value[1];

	if ((disable < 0) || (disable > 1)) {
		pr_err(" %s Invalid arguments: %d\n", __func__, disable);

		ret = -EINVAL;
		goto done;
	}
	pr_debug("%s: disable = %d, session_id = %d\n", __func__, disable,
		 session_id);

	ret = voc_disable_topology(session_id, disable);

done:
	return ret;
}

static int msm_voice_cvd_version_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	int ret = 0;

	pr_debug("%s:\n", __func__);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = CVD_VERSION_STRING_MAX_SIZE;

	return ret;
}

static int msm_voice_cvd_version_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	char cvd_version[CVD_VERSION_STRING_MAX_SIZE] = CVD_VERSION_DEFAULT;
	int ret;

	pr_debug("%s:\n", __func__);

	ret = voc_get_cvd_version(cvd_version);

	if (ret)
		pr_err("%s: Error retrieving CVD version, error:%d\n",
			__func__, ret);

	memcpy(ucontrol->value.bytes.data, cvd_version, sizeof(cvd_version));

	return 0;
}

static struct snd_kcontrol_new msm_voice_controls[] = {
	SOC_SINGLE_MULTI_EXT("Voice Rx Device Mute", SND_SOC_NOPM, 0, VSID_MAX,
				0, 3, NULL, msm_voice_rx_device_mute_put),
	SOC_SINGLE_MULTI_EXT("Voice Tx Device Mute", SND_SOC_NOPM, 0, VSID_MAX,
				0, 3, NULL, msm_voice_tx_device_mute_put),
	SOC_SINGLE_MULTI_EXT("Voice Tx Mute", SND_SOC_NOPM, 0, VSID_MAX,
				0, 3, NULL, msm_voice_mute_put),
	SOC_SINGLE_MULTI_EXT("Voice Rx Gain", SND_SOC_NOPM, 0, VSID_MAX, 0, 3,
				NULL, msm_voice_gain_put),
	SOC_ENUM_EXT("TTY Mode", msm_tty_mode_enum[0], msm_voice_tty_mode_get,
				msm_voice_tty_mode_put),
	SOC_SINGLE_MULTI_EXT("Slowtalk Enable", SND_SOC_NOPM, 0, VSID_MAX, 0, 2,
				NULL, msm_voice_slowtalk_put),
	SOC_SINGLE_MULTI_EXT("Voice Topology Disable", SND_SOC_NOPM, 0,
			     VSID_MAX, 0, 2, NULL,
			     msm_voice_topology_disable_put),
	SOC_SINGLE_MULTI_EXT("HD Voice Enable", SND_SOC_NOPM, 0, VSID_MAX, 0, 2,
			     NULL, msm_voice_hd_voice_put),
	SOC_SINGLE_MULTI_EXT("DTMF_Detect Rx VSID enable",
				SND_SOC_NOPM, 0, VSID_MAX, 0, 2,
				NULL, msm_dtmf_detect_rx_vsid_put),
	SOC_SINGLE_MULTI_EXT("DTMF_Detect Rx Callback VSID enable",
				SND_SOC_NOPM, 0, VSID_MAX, 0, 2,
				NULL, msm_dtmf_detect_rx_vsid_cb_put),
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= "CVD Version",
		.info	= msm_voice_cvd_version_info,
		.get	= msm_voice_cvd_version_get,
	},
	SOC_SINGLE_MULTI_EXT("Voice Sidetone Enable", SND_SOC_NOPM, 0, 1, 0, 1,
			     msm_voice_sidetone_get, msm_voice_sidetone_put),

};

static int msm_pcm_adsp_stream_cmd_put(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int msm_pcm_add_voice_adsp_stream_cmd_control(
			struct snd_soc_pcm_runtime *rtd)
{
	const char *mixer_ctl_name = DSP_STREAM_CMD;
	const char *deviceNo = "NN";
	char *mixer_str = NULL;
	int ctl_len = 0, ret = 0;
	struct snd_kcontrol_new fe_voice_adsp_stream_cmd_config_control[1] = {
		{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "?",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = msm_adsp_stream_cmd_info,
		.put = msm_pcm_adsp_stream_cmd_put,
		.private_value = 0,
		}
	};

	if (!rtd) {
		pr_err("%s rtd is NULL\n", __func__);
		return -EINVAL;
	}

	ctl_len = strlen(mixer_ctl_name) + 1 + strlen(deviceNo) + 1;
	mixer_str = kzalloc(ctl_len, GFP_KERNEL);
	if (!mixer_str)
		return -ENOMEM;

	snprintf(mixer_str, ctl_len, "%s %d", mixer_ctl_name, rtd->pcm->device);
	fe_voice_adsp_stream_cmd_config_control[0].name = mixer_str;
	fe_voice_adsp_stream_cmd_config_control[0].private_value =
		rtd->dai_link->be_id;
	pr_debug("Registering new mixer ctl %s\n", mixer_str);
	ret = snd_soc_add_platform_controls(rtd->platform,
		fe_voice_adsp_stream_cmd_config_control,
		ARRAY_SIZE(fe_voice_adsp_stream_cmd_config_control));
	if (ret < 0)
		pr_err("%s: failed add ctl %s. err = %d\n",
			__func__, mixer_str, ret);

	kfree(mixer_str);
	return ret;
}

static int msm_pcm_add_voice_adsp_stream_callback_control(
			struct snd_soc_pcm_runtime *rtd)
{
	const char *mixer_ctl_name = DSP_STREAM_CALLBACK;
	const char *deviceNo = "NN";
	char *mixer_str = NULL;
	int ctl_len = 0, ret = 0;
	struct snd_kcontrol *kctl;

	struct snd_kcontrol_new fe_voice_adsp_callback_config_control[1] = {
		{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "?",
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = msm_adsp_stream_callback_info,
		.get = msm_adsp_stream_callback_get,
		.private_value = 0,
		}
	};

	if (!rtd) {
		pr_err("%s NULL rtd\n", __func__);
		return -EINVAL;
	}

	pr_debug("%s: added new pcm FE with name %s, id %d, cpu dai %s, device no %d\n",
		 __func__, rtd->dai_link->name, rtd->dai_link->be_id,
		 rtd->dai_link->cpu_dai_name, rtd->pcm->device);
	ctl_len = strlen(mixer_ctl_name) + 1 + strlen(deviceNo) + 1;
	mixer_str = kzalloc(ctl_len, GFP_KERNEL);
	if (!mixer_str)
		return -ENOMEM;

	snprintf(mixer_str, ctl_len, "%s %d", mixer_ctl_name, rtd->pcm->device);
	fe_voice_adsp_callback_config_control[0].name = mixer_str;
	fe_voice_adsp_callback_config_control[0].private_value =
		rtd->dai_link->be_id;
	pr_debug("%s: Registering new mixer ctl %s\n", __func__, mixer_str);
	ret = snd_soc_add_platform_controls(rtd->platform,
			fe_voice_adsp_callback_config_control,
			ARRAY_SIZE(fe_voice_adsp_callback_config_control));
	if (ret < 0) {
		pr_err("%s: failed to add ctl %s. err = %d\n",
			__func__, mixer_str, ret);
		ret = -EINVAL;
		goto free_mixer_str;
	}

	kctl = snd_soc_card_get_kcontrol(rtd->card, mixer_str);
	if (!kctl) {
		pr_err("%s: failed to get kctl %s.\n", __func__, mixer_str);
		ret = -EINVAL;
		goto free_mixer_str;
	}

	kctl->private_data = NULL;

free_mixer_str:
	kfree(mixer_str);
	return ret;
}

static struct snd_pcm_ops msm_pcm_ops = {
	.open			= msm_pcm_open,
	.hw_params		= msm_pcm_hw_params,
	.close			= msm_pcm_close,
	.prepare		= msm_pcm_prepare,
	.trigger		= msm_pcm_trigger,
	.ioctl			= msm_pcm_ioctl,
	.compat_ioctl		= msm_pcm_ioctl,
};


static int msm_asoc_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_card *card = rtd->card->snd_card;
	int ret = 0;

	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = DMA_BIT_MASK(32);

	ret = msm_pcm_add_voice_adsp_stream_cmd_control(rtd);
	if (ret)
		pr_err("%s: Could not add pcm ADSP Stream Cmd Control\n",
			__func__);

	ret = msm_pcm_add_voice_adsp_stream_callback_control(rtd);
	if (ret)
		pr_err("%s: Could not add pcm ADSP Stream Callback Control\n",
			__func__);

	return ret;
}

static int msm_pcm_voice_probe(struct snd_soc_platform *platform)
{
	snd_soc_add_platform_controls(platform, msm_voice_controls,
					ARRAY_SIZE(msm_voice_controls));

	return 0;
}

static struct snd_soc_platform_driver msm_soc_platform = {
	.ops		= &msm_pcm_ops,
	.pcm_new	= msm_asoc_pcm_new,
	.probe		= msm_pcm_voice_probe,
};

static int msm_pcm_probe(struct platform_device *pdev)
{
	int rc;
	bool destroy_cvd = false;
	bool vote_bms = false;
	const char *is_destroy_cvd = "qcom,destroy-cvd";
	const char *is_vote_bms = "qcom,vote-bms";

	if (!is_voc_initialized()) {
		pr_debug("%s: voice module not initialized yet, deferring probe()\n",
		       __func__);

		rc = -EPROBE_DEFER;
		goto done;
	}

	rc = voc_alloc_cal_shared_memory();
	if (rc == -EPROBE_DEFER) {
		pr_debug("%s: memory allocation for calibration deferred %d\n",
			 __func__, rc);

		goto done;
	} else if (rc < 0) {
		pr_err("%s: memory allocation for calibration failed %d\n",
		       __func__, rc);
	}

	pr_debug("%s: dev name %s\n",
			__func__, dev_name(&pdev->dev));
	destroy_cvd = of_property_read_bool(pdev->dev.of_node,
						is_destroy_cvd);
	voc_set_destroy_cvd_flag(destroy_cvd);

	vote_bms = of_property_read_bool(pdev->dev.of_node,
					 is_vote_bms);
	voc_set_vote_bms_flag(vote_bms);

	rc = snd_soc_register_platform(&pdev->dev,
				       &msm_soc_platform);

done:
	return rc;
}

static int msm_pcm_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static const struct of_device_id msm_voice_dt_match[] = {
	{.compatible = "qcom,msm-pcm-voice"},
	{}
};
MODULE_DEVICE_TABLE(of, msm_voice_dt_match);

static struct platform_driver msm_pcm_driver = {
	.driver = {
		.name = "msm-pcm-voice",
		.owner = THIS_MODULE,
		.of_match_table = msm_voice_dt_match,
	},
	.probe = msm_pcm_probe,
	.remove = msm_pcm_remove,
};

static int __init msm_soc_platform_init(void)
{
	int i = 0;

	memset(&voice_info, 0, sizeof(voice_info));

	for (i = 0; i < VOICE_SESSION_INDEX_MAX; i++)
		mutex_init(&voice_info[i].lock);

	return platform_driver_register(&msm_pcm_driver);
}
module_init(msm_soc_platform_init);

static void __exit msm_soc_platform_exit(void)
{
	platform_driver_unregister(&msm_pcm_driver);
}
module_exit(msm_soc_platform_exit);

MODULE_DESCRIPTION("Voice PCM module platform driver");
MODULE_LICENSE("GPL v2");
