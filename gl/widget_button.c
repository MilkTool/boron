/*
  Boron OpenGL GUI
  Copyright 2008-2010 Karl Robillard

  This file is part of the Boron programming language.

  Boron is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Boron is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with Boron.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <GL/glv_keys.h>
#include "boron-gl.h"
#include "draw_prog.h"
#include "os.h"
#include "gl_atoms.h"


enum ButtonState
{
    BTN_STATE_UP,
    BTN_STATE_DOWN
};

typedef struct
{
    GWidget  wid;
    UIndex   labelN;
    UIndex   actionN;
    DPSwitch dpSwitch;
    uint8_t  state;
    uint8_t  held;
    uint8_t  checked;
}
Button;

#define EX_PTR  Button* ep = (Button*) wp


GWidgetClass wclass_button;
GWidgetClass wclass_checkbox;


static GWidget* button_make( UThread* ut, UBlockIter* bi )
{
    if( (bi->end - bi->it) > 2 )
    {
        Button* ep;
        const UCell* arg = ++bi->it;

        if( ! ur_is(arg, UT_STRING) )
        {
            ur_error( ut, UR_ERR_SCRIPT, "button expected string! text" );
            return 0;
        }
        if( ! ur_is(arg + 1, UT_BLOCK) )
        {
            ur_error( ut, UR_ERR_SCRIPT, "button expected block! action" );
            return 0;
        }

        ep = (Button*) gui_allocWidget( sizeof(Button), &wclass_button );
        ep->state   = BTN_STATE_UP;
        ep->labelN  = arg[0].series.buf;
        ep->actionN = arg[1].series.buf;

        bi->it = arg + 2;
        return (GWidget*) ep;
    }
    ur_error( ut, UR_ERR_SCRIPT, "button expected string! text" );
    return 0;
}


static void button_free( GWidget* wp )
{
    memFree( wp );
}


static void button_mark( UThread* ut, GWidget* wp )
{
    EX_PTR;
    ur_markBuffer( ut, ep->labelN );
    ur_markBlkN( ut, ep->actionN );
}


static void button_setState( UThread* ut, Button* ep, int state )
{
    if( state != ep->state )
    {
        ep->state = state;

        if( ep->dpSwitch )
        {
            UIndex resN = gui_parentDrawProg( &ep->wid );
            if( resN != UR_INVALID_BUF )
                ur_setDPSwitch( ut, resN, ep->dpSwitch, state );
        }
    }
}


static void button_dispatch( UThread* ut, GWidget* wp, const GLViewEvent* ev )
{
    EX_PTR;
    //printf( "KR button %d\n", ev->type );

    switch( ev->type )
    {
        case GLV_EVENT_BUTTON_DOWN:
            if( ev->code == GLV_BUTTON_LEFT )
            {
                button_setState( ut, ep, BTN_STATE_DOWN );
                ep->held = 1;
                //gui_grabMouse( ui, wp );
                //gui_setKeyFocus( ui, wp );
            }
            break;

        case GLV_EVENT_BUTTON_UP:
            if( ev->code == GLV_BUTTON_LEFT )
            {
                int pressed = (ep->state == BTN_STATE_DOWN);
                //gui_ungrabMouse( ui, wp );
                button_setState( ut, ep, BTN_STATE_UP );
                ep->held = 0;
                if( pressed && gui_widgetContains( wp, ev->x, ev->y ) )
                    goto activate;
            }
            break;

        case GLV_EVENT_MOTION:
            if( ep->held )
            {
                button_setState( ut, ep,
                                 gui_widgetContains( wp, ev->x, ev->y ) ?
                                         BTN_STATE_DOWN : BTN_STATE_UP );
            }
            break;

        case GLV_EVENT_KEY_DOWN:
            if( ev->code == KEY_Return )
            {
                button_setState( ut, ep, BTN_STATE_UP );
                goto activate;
            }
            break;

        case GLV_EVENT_FOCUS_IN:
            break;

        case GLV_EVENT_FOCUS_OUT:
            button_setState( ut, ep, BTN_STATE_UP );
            break;
    }
    return;

activate:

    if( ep->actionN )
        gui_doBlockN( ut, ep->actionN );
}


static void check_dispatch( UThread* ut, GWidget* wp, const GLViewEvent* ev )
{
    EX_PTR;

    switch( ev->type )
    {
        case GLV_EVENT_BUTTON_DOWN:
            if( ev->code == GLV_BUTTON_LEFT )
            {
                button_setState( ut, ep, ep->checked ^ 1 );
                ep->held = 1;
                //gui_grabMouse( ui, wp );
                //gui_setKeyFocus( ui, wp );
            }
            break;

        case GLV_EVENT_BUTTON_UP:
            if( ev->code == GLV_BUTTON_LEFT )
            {
                int pressed = (ep->state != ep->checked);
                //gui_ungrabMouse( ui, wp );
                ep->held = 0;
                if( pressed && gui_widgetContains( wp, ev->x, ev->y ) )
                    goto activate;
            }
            break;

        case GLV_EVENT_MOTION:
            if( ep->held )
            {
                button_setState( ut, ep,
                                 gui_widgetContains( wp, ev->x, ev->y ) ?
                                    ep->checked ^ 1 : ep->checked );
            }
            break;

        case GLV_EVENT_KEY_DOWN:
            if( ev->code == KEY_Return )
                goto activate;
            break;

        case GLV_EVENT_FOCUS_IN:
        case GLV_EVENT_FOCUS_OUT:
            break;
    }
    return;

activate:

    if( ep->actionN )
    {
        ep->checked ^= 1;
        button_setState( ut, ep, ep->checked );

        /*
        UR_S_GROW;
        ur_initType(UR_TOS, UT_LOGIC);
        ur_logic(UR_TOS) = ep->checked;
        */

        gui_doBlockN( ut, ep->actionN );
    }
}


static void button_sizeHint( GWidget* wp, GSizeHint* size )
{
    UCell* rc;
    UCell* style;
    UBuffer* str;
    TexFont* tf;
    EX_PTR;
    UThread* ut = glEnv.guiUT;
    int width;

    style = gui_style( ut );
    rc = style + ((wp->wclass == &wclass_checkbox) ?
                        CI_STYLE_CHECKBOX_SIZE : CI_STYLE_BUTTON_SIZE);
    if( ur_is(rc, UT_COORD) )
    {
        size->minW = rc->coord.n[0];
        size->minH = rc->coord.n[1];
        size->maxW = rc->coord.n[2];
    }
    else
    {
        size->minW = 32;
        size->minH = 20;
        size->maxW = 100;
    }

    size->maxH    = size->minH;
    size->weightX = 2;
    size->weightY = 1;
    size->policyX = GW_WEIGHTED;
    size->policyY = GW_FIXED;

    if( ep->labelN )
    {
        str = ur_buffer( ep->labelN );
        tf = ur_texFontV( ut, style + CI_STYLE_CONTROL_FONT );
        if( tf )
        {
            width = txf_width( tf, str->ptr.b, str->ptr.b + str->used );
            if( width > size->minW )
                size->minW += width;
            if( size->maxW < size->minW )
                size->maxW = size->minW;
        }
    }
}


static void button_layout( UThread* ut, GWidget* wp )
{
    UCell* rc;
    UCell* style;
    EX_PTR;
    int check = (wp->wclass == &wclass_checkbox);

    if( ! gDPC )
        return;

    style = gui_style( ut );

    // Set draw list variables.

    rc = style + CI_STYLE_LABEL;
    ur_setId( rc, UT_STRING );
    ur_setSeries( rc, ep->labelN, 0 );

    rc = style + CI_STYLE_AREA;
    gui_initRectCoord( rc, wp, UR_ATOM_RECT );


    // Compile draw lists.

    ep->dpSwitch = dp_beginSwitch( gDPC, 2 );

    rc = style + (check ? CI_STYLE_CHECKBOX_UP : CI_STYLE_BUTTON_UP);
    if( ur_is(rc, UT_BLOCK) )
        ur_compileDP( ut, rc, 1 );
    dp_endCase( gDPC, ep->dpSwitch );

    rc = style + (check ? CI_STYLE_CHECKBOX_DOWN : CI_STYLE_BUTTON_DOWN);
    if( ur_is(rc, UT_BLOCK) )
        ur_compileDP( ut, rc, 1 );
    dp_endCase( gDPC, ep->dpSwitch );

    dp_endSwitch( gDPC, ep->dpSwitch, ep->state );
}


static void button_render( GUI* ui, GWidget* wp )
{
    (void) ui;
    (void) wp;
}


static int button_select( GWidget* wp, UAtom atom, UCell* res )
{
    EX_PTR;
    if( atom == UR_ATOM_TEXT )
    {
        ur_setId(res, UT_STRING);
        ur_setSeries(res, ep->labelN, 0);
        return 1;
    }
    if( atom == UR_ATOM_ACTION )
    {
        ur_setId(res, UT_BLOCK);
        ur_setSeries(res, ep->actionN, 0);
        return 1;
    }
    return gui_areaSelect( wp, atom, res );
}


GWidgetClass wclass_button =
{
    "button",
    button_make,       button_free,       button_mark,
    button_dispatch,   button_sizeHint,   button_layout,
    button_render,     button_select,
    0, 0
};


GWidgetClass wclass_checkbox =
{
    "checkbox",
    button_make,       button_free,       button_mark,
    check_dispatch,    button_sizeHint,   button_layout,
    button_render,     button_select,
    0, 0
};


// EOF
