/*

  Copyright (c) 2003,2004 uim Project http://uim.freedesktop.org/

  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  1. Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.
  3. Neither the name of authors nor the names of its contributors
     may be used to endorse or promote products derived from this software
     without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
  SUCH DAMAGE.
*/

// Handle IC (Input Context) part of XIM protocol.
// Intermediate key handling between backend IM engine.
 
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
 
#include <stdlib.h>
#include <ctype.h>
#include "xim.h"
#include "convdisp.h"
#include "ximserver.h"
#include "connection.h"
 
#ifndef __GNUC__
# ifdef HAVE_ALLOCA_H
#  include <alloca.h>
# endif
#endif

char invalid_style_msg[]=
"Client requested unsupported input style";

XimIC *XimIC::current_ic;
int XimIC::nrActiveIC;

struct FSCache {
    int refc;
    XFontSet fs;
    char *name;
    char *locale;
};

static std::list<FSCache> fs_cache;

XFontSet
get_font_set(const char *name, const char *locale)
{
    std::list<FSCache>::iterator it;
    for (it = fs_cache.begin(); it != fs_cache.end(); it++) {
	if (!strcmp(it->name, name) && !strcmp(it->locale, locale)) {
	    it->refc++;
	    return it->fs;
	}
    }
    struct FSCache fc;
    char **missing, *def;
    int nr_missing;
    fc.fs = XCreateFontSet(XimServer::gDpy, name,
			   &missing, &nr_missing, &def);
    if (missing)
	XFreeStringList(missing);

    fc.refc = 1;
    fc.name = strdup(name);
    fc.locale = strdup(locale);
    fs_cache.push_back(fc);
    return fc.fs;
}

static void
release_font_set(const char *name, const char *locale)
{
    std::list<FSCache>::iterator it;
    for (it = fs_cache.begin(); it != fs_cache.end(); it++) {
	if (!strcmp(it->name, name) && !strcmp(it->locale, locale)) {
	    it->refc--;
	    if (!it->refc) {
		XFreeFontSet(XimServer::gDpy, it->fs);
		free(it->name);
		free(it->locale);
		fs_cache.erase(it);
	    }
	    return;
	}
    }
}

pe_stat::pe_stat(InputContext *c)
{
    cont = c;
    clear();
}

void pe_stat::clear()
{
    ustrings.erase(ustrings.begin(), ustrings.end());
    caret_pos = 0;
}

void pe_stat::new_segment(int s)
{
    pe_ustring p;
    p.stat = s;
    ustrings.push_back(p);
}

void pe_stat::push_uchar(uchar c)
{
    std::list<pe_ustring>::reverse_iterator i = ustrings.rbegin();
    (*i).s.push_back(c);
}

int pe_stat::get_char_count()
{
    std::list<pe_ustring>::iterator i;
    uString::iterator j;
    int k = 0;
    for (i = ustrings.begin(); i != ustrings.end(); i++) {
	for (j = (*i).s.begin(); j != (*i).s.end(); j++) {
	    k++;
	}
    }
    return k;
}

icxatr::icxatr()
{
    atr_mask = 0;
    change_mask = 0;
    font_set_name = NULL;
    font_set = NULL;
    m_locale = NULL;
    foreground_pixel = BlackPixel(XimServer::gDpy, DefaultScreen(XimServer::gDpy));
    background_pixel = WhitePixel(XimServer::gDpy, DefaultScreen(XimServer::gDpy));
}

icxatr::~icxatr()
{
    if (font_set_name) {
	release_font_set(font_set_name, m_locale);
	free((void *)font_set_name);
    }
    if (m_locale)
	free(m_locale);
}

bool icxatr::has_atr(int id)
{
    return atr_mask & (1 << id);
}

void icxatr::set_atr(int id, C8 *val, int len, int o)
{
    switch (id) {
    case ICA_InputStyle:
	input_style = readC32(val, o);
	break;
    case ICA_ClientWindow:
	client_window = readC32(val, o);
	break;
    case ICA_FocusWindow:
	focus_window = readC32(val, o);
	break;
    case ICA_Foreground:
	foreground_pixel = readC32(val, o);
	break;
    case ICA_Background:
	background_pixel = readC32(val, o);
	break;
    case ICA_SpotLocation:
	spot_location.x = readC16(val, o);
	spot_location.y = readC16(&val[2], o);
	break;
    case ICA_FontSet:
    {
	int len = readC16(val, o);
	char *new_fsn = (char *)alloca(len + 1);
	new_fsn[len] = 0;
	memcpy(new_fsn, &val[2], len);
	if (font_set_name && !strcmp(font_set_name, new_fsn))
	    break;

	if (font_set_name)
	    free(font_set_name);

	font_set_name = strdup(new_fsn);
	font_set = get_font_set(font_set_name, m_locale);
    }
    break;
    case ICA_Area:
    {
	area.x = readC16(&val[0], o);
	area.y = readC16(&val[2], o);
	area.width = readC16(&val[4], o);
	area.height = readC16(&val[6], o);
    }
    break;
    case ICA_LineSpace:
	line_space=readC16(val, o);
	break;
    default:
	// unknown attribute
	printf("try to set unknown ic attribute %d.\n", id);
	return;
    }
    atr_mask |= (1 << id);
    change_mask |= (1 << id);
}

bool icxatr::is_changed(int id)
{
    if (change_mask & (1 << id))
	return true;

    return false;
}

void icxatr::unset_change_mask(int id)
{
    change_mask &=(~(1 << id));
}

void icxatr::print()
{
    if (has_atr(ICA_InputStyle))
	printf("input-style %ld.\n", input_style);
    else
	printf("input-style undefined.\n");

    if (has_atr(ICA_ClientWindow))
	printf("client-window id %ld.\n", client_window);
    else
	printf("client-window id undefined.\n");

    if (has_atr(ICA_FocusWindow))
	printf("focus-window id %ld.\n", focus_window);
    else
	printf("focus-window id undefined.\n");

    if (has_atr(ICA_Foreground))
	printf("foreground-pixel %d.\n", foreground_pixel);
    else
	printf("foreground-pixel undefined.\n");

    if (has_atr(ICA_Background))
	printf("background-pixel %d.\n", background_pixel);
    else
	printf("background-pixel undefined.\n");

    if (has_atr(ICA_SpotLocation))
	printf("spot location x=%d,y=%d.\n",
	       spot_location.x, spot_location.y);
    else
	printf("spot location undefined.\n");

    if (has_atr(ICA_FontSet))
	printf("font-set-name %s\n", font_set_name);
    else
	printf("font-set-name undefined.\n");

    if (has_atr(ICA_Area))
	printf("area = x=%d y=%d width=%d height=%d.\n",
	       area.x, area.y, area.width, area.height);
    else
	printf("area undefined.\n");

    if (has_atr(ICA_LineSpace))
	printf("line-space %d.\n", line_space);
    else
	printf("line-space undefined.\n");

}

int icxatr::getSize(int id)
{
    switch (id) {
    case ICA_FocusWindow:
	return 4;
    case ICA_FilterEvents:
	return 4;
    case ICA_InputStyle:
	return 4;
    }
    return 0;
}

void icxatr::set_locale_name(const char *locale)
{
    if (m_locale)
	free(m_locale);
    m_locale = strdup(locale);
}

XimIC::XimIC(Connection *c, int imid, int icid, const char *engine)
{
    mConn = c;
    mIMid = imid;
    mICid = icid;
    mIsActive = false;

    XimServer *svr = mConn->getXimServer();
    m_kkContext = svr->createContext(this, engine);

    const char *locale = m_kkContext->get_locale_name();
    m_xatr.set_locale_name(locale);

    mConvdisp = 0;
    if (g_option_mask & OPT_TRACE)
	printf("imid=%d, icid=%d ic created.\n", mIMid, mICid);
}

XimIC::~XimIC()
{
    if (g_option_mask & OPT_TRACE)
	printf("imid=%d, icid=%d ic deleted.\n", mIMid , mICid);

    unsetFocus();
    if (current_ic == this)
	current_ic = 0;

    // The sequence is important.
    delete m_kkContext;
    if (mConvdisp)
	delete mConvdisp;
}

bool XimIC::isActive()
{
    return mIsActive;
}

int XimIC::get_icid()
{
    return mICid;
}

int XimIC::get_imid()
{
    return mIMid;
}

void XimIC::setFocus()
{
    if (!mIsActive)
	nrActiveIC++;

    current_ic = this;
    mIsActive = true;
    m_kkContext->focusIn();

    if (mConvdisp) {
	// Updating preedit here causes string mismatch if the context
	// receives XIM_RESET_IC after XIM_SET_IC_FOCUS.  Should only
	// update candidate window.
	if (m_kkContext->hasActiveCandwin()) {
	    mConvdisp->move_candwin();
	    m_kkContext->candidate_update();
	} else {
	    mConvdisp->unset_focus();
	}
    }
}

void XimIC::unsetFocus()
{
    if (!mIsActive)
	return;

    mIsActive = false;
    nrActiveIC--;
    m_kkContext->focusOut();
    // Since the sequence of XIM_SET_IC_FOCUS and XIM_UNSET_FOCUS
    // events is not consistent, unsetting focus of candidate window
    // is now handled in XimIC::setFocus() and focus_in message from
    // helper application.
    //
    // if (mConvdisp && m_kkContext->hasActiveCandwin()) {
    //     mConvdisp->unset_focus();
    // }
}

void XimIC::OnKeyEvent(keyEventX e)
{
    int s;
    m_keyState.check_key(&e);

    s = m_kkContext->pushKey(&m_keyState);
    if (s & COMMIT_RAW)
	send_key_event(&e.ev.xkey);

    if (s & UPDATE_MODE)
	onSendPacket(); // send XIM_COMMIT
}

void XimIC::changeContext(const char *engine)
{
   m_kkContext->changeContext(engine);
}

void XimIC::send_key_event(XKeyEvent *e)
{
    TxPacket *t;
    t = createTxPacket(XIM_FORWARD_EVENT, 0);
    t->pushC16(mIMid);
    t->pushC16(mICid);
    t->pushC16(1); // flag, synchronous
    t->pushC16((e->serial >> 16) & 0xffff);

    t->pushC8(e->type);
    t->pushC8(e->keycode);
    t->pushC16(e->serial & 0xffff);
    t->pushC32(e->time);
    t->pushC32(e->root);
    t->pushC32(e->window);
    t->pushC32(e->subwindow);
    t->pushC16(e->x_root);
    t->pushC16(e->y_root);
    t->pushC16(e->x);
    t->pushC16(e->y);
    t->pushC16(e->state);
    t->pushC8(e->same_screen);
    t->pushC8(0);
    mConn->push_packet(t);
}

void XimIC::commit_string(const char *str)
{
    uString us;
    mConn->getXimServer()->strToUstring(&us, str);
    append_ustring(&mPending, &us);
}

void XimIC::extra_input(char *s)
{
    if (!m_kkContext->extra_input(s))
	commit_string(s);
    if (m_xatr.has_atr(ICA_FocusWindow))
	force_event(m_xatr.focus_window);
}

void XimIC::force_send_packet(void) {
    (dynamic_cast<XConnection *>(mConn))->writeProc();
}

void XimIC::setICAttrs(void *val, int len)
{
    unsigned char *p=(unsigned char *)val;
    int byte_order = mConn->byte_order();
    int i;
    for (i = 0; i < len;) {
	int atr_id, atr_len;
	atr_id = readC16(&p[i], byte_order);
	i += 2;

	atr_len = readC16(&p[i], byte_order);
	i += 2;

	unsigned char *q;
	q = (unsigned char *)alloca(atr_len + pad4(atr_len));

	int j;
	for (j = 0; j < atr_len + pad4(atr_len); j++, i++) {
	    q[j] = p[i];
	}

	set_ic_attr(atr_id, (C8 *)q, atr_len);
    }
}

int XimIC::get_ic_atr(int id, TxPacket *t)
{
    int l = m_xatr.getSize(id);
    if (!t)
	return l;

    switch (id) {
    case ICA_FocusWindow:
	t->pushC32(m_xatr.focus_window);
	break;
    case ICA_FilterEvents:
	if (g_option_mask & OPT_ON_DEMAND_SYNC)
	    t->pushC32(KeyPressMask|KeyReleaseMask);
	else // Filtering KeyRelease event with full-synchronous method
	     //	causes problem with mozilla (1.7.3) gtk2 on navigation
	     //	toolbar's auto text completion...
	    t->pushC32(KeyPressMask); 
	break;
    case ICA_InputStyle:
	t->pushC32(m_xatr.input_style);
  	break;
    default:
	printf("try to get unknown ic attribute %d.\n", id);
	break;
    }
    return l;
}

int XimIC::lookup_style(unsigned long s)
{
    int i;
    struct input_style *is = mConn->getXimServer()->getInputStyles();
    for (i = 0; is[i].x_style; i++) {
	if (is[i].x_style == (int)s)
	    return is[i].style;
    }
    return IS_INVALID;
}

void XimIC::set_ic_attr(int id, C8 *val, int len)
{
    if (id == ICA_PreeditAttribute || id == ICA_StatusAttributes)
	setICAttrs(val, len); // list of attribute
    else
	m_xatr.set_atr(id, val, len, mConn->byte_order());

    if (mConvdisp)
	mConvdisp->update_icxatr();
    else {
	if (m_xatr.has_atr(ICA_InputStyle)) {
	    mConvdisp = create_convdisp(lookup_style(m_xatr.input_style),
				    m_kkContext, &m_xatr, mConn);
	    m_kkContext->setConvdisp(mConvdisp);

	    if (mConvdisp)
		mConvdisp->update_icxatr();
	}
    }
}

void XimIC::reset_ic()
{
    TxPacket *t;
    t = createTxPacket(XIM_RESET_IC_REPLY, 0);
    t->pushC16(mIMid);
    t->pushC16(mICid);

    uString s;
    const char *encoding = get_encoding();

    // m_kkContext->get_preedit_string() returns uncommitted preedit
    // strings, which will be committed in client applications.
    s = m_kkContext->get_preedit_string();
    if (s.size()) {
	char *p;
	int i, len = 0;
	p = mConn->getXimServer()->uStringToCtext(&s, encoding);
	if (p) {
	    len = strlen(p);
	    t->pushC16(len); // length of committed strings
	    for (i = 0; i < len; i++) {
		t->pushC8(p[i]); // put string here
	    }
	    len = pad4(len + 2);
	    for (i = 0; i < len; i++) {
		t->pushC8(0); // padding (len + 2bytes)
	    }
	    free(p);
	} else {
	    t->pushC16(0);
	    t->pushC16(0);
	}
    } else {
	t->pushC16(0);
	t->pushC16(0);
    }

    mConn->push_packet(t);

    // After sending RESET_IC_REPLY, uim-xim needs to clear and reset
    // the input context.
    m_kkContext->clear();
}

Convdisp *XimIC::get_convdisp()
{
    return mConvdisp;
}

void XimIC::onSendPacket()
{
    if (!mPending.size())
	return;

    char *p;
    const char *encoding = get_encoding();
    p = mConn->getXimServer()->uStringToCtext(&mPending, encoding);

    if (!p) {
	erase_ustring(&mPending);
	return;
    }

    TxPacket *t;
    t = createTxPacket(XIM_COMMIT, 0);
    t->pushC16(mIMid);
    t->pushC16(mICid);

    t->pushC16(3); // XLookupChars|synchronous

    int i, len;
    len = strlen(p);

    t->pushC16(len);
    for (i = 0; i < len; i++) {
	t->pushC8(p[i]);
    }
    len = pad4(len);
    for (i = 0; i < len; i++) {
	t->pushC8(0);
    }
    free(p);

    mConn->push_passive_packet(t);

    erase_ustring(&mPending);
}

XimIC *XimIC::get_current_ic()
{
    return current_ic;
}

bool XimIC::isAnyActive()
{
    if (nrActiveIC)
	return true;

    return false;
}

const char *XimIC::get_encoding()
{
    XimIM *im = get_im_by_id(mIMid);
    return im->get_encoding();
}

const char *XimIC::get_lang_region()
{
    XimIM *im = get_im_by_id(mIMid);
    return im->get_lang_region();
}

XimIC *create_ic(Connection *c, RxPacket *p, int imid, int icid, const char *engine)
{
    XimIC *ic;
    ic = new XimIC(c, imid, icid, engine);
    p->rewind();
    p->getC16(); // discard
    int atr_len = p->getC16();
    unsigned char *v;
    v = (unsigned char *)alloca(atr_len);
    int i;
    for (i = 0; i < atr_len; i++) {
	v[i] = p->getC8();
    }
    ic->setICAttrs((void *)v, atr_len);
    if (!ic->get_convdisp()) {
	delete ic;
	return 0;
    }
    return ic;
}
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 */
