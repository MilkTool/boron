/*
  Boron OpenGL GUI
  Copyright 2012 Karl Robillard

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


enum SliderState
{
    BTN_STATE_UP,
    BTN_STATE_DOWN
};

enum SliderOrient
{
    HSLIDER,
    VSLIDER
};

struct ValueI
{
    int32_t val, min, max;
};

struct ValueF
{
    float val, min, max;
};

typedef struct
{
    GWidget  wid;
    union
    {
        struct ValueF f;
        struct ValueI i;
    } data;
    UIndex   actionN;
    //DPSwitch dpSwitch;
    int16_t  tx;
    DPSwitch dpTrans;
    uint8_t  state;
    uint8_t  held;
    uint8_t  orient;
    uint8_t  dataType;
}
GSlider;        

#define EX_PTR  GSlider* ep = (GSlider*) wp


/*
  slider min,max <action>
*/
static GWidget* slider_make( UThread* ut, UBlockIter* bi,
                             const GWidgetClass* wclass )
{
    GSlider* ep;
    int type;
    const UCell* arg = bi->it;

    if( ++arg == bi->end )
        goto bad_arg;
    type = ur_type(arg);
    if( type != UT_COORD && type != UT_VEC3 )
        goto bad_arg;

    ep = (GSlider*) gui_allocWidget( sizeof(GSlider), wclass );
    ep->state = BTN_STATE_UP;
    if( type == UT_COORD )
    {
        ep->data.i.val =
        ep->data.i.min = arg->coord.n[0];
        ep->data.i.max = arg->coord.n[1];
        ep->dataType = UT_INT;
    }
    else
    {
        ep->data.f.val =
        ep->data.f.min = arg->vec3.xyz[0];
        ep->data.f.max = arg->vec3.xyz[1];
        ep->dataType = UT_DECIMAL;
    }

    // Optional action block.
    if( ++arg != bi->end )
    {
        if( ur_is(arg, UT_BLOCK) )
        {
            ep->actionN = arg->series.buf;
            ++arg;
        }
    }

    bi->it = arg;
    return (GWidget*) ep;

bad_arg:
    ur_error( ut, UR_ERR_SCRIPT, "slider expected min,max coord!/vec3!" );
    return 0;
}


static void slider_mark( UThread* ut, GWidget* wp )
{
    EX_PTR;
    //ur_markBuffer( ut, ep->labelN );
    ur_markBlkN( ut, ep->actionN );
}


/*
static void slider_setState( UThread* ut, GSlider* ep, int state )
{
    (void) ut;

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
*/


static int slider_adjust( GSlider* ep, int adj )
{
    int nx = ep->tx + adj;
    if( nx < 0 )
        nx = 0;
    else if( nx > ep->wid.area.w )
        nx = ep->wid.area.w;
    if( ep->tx != nx )
    {
        ep->tx = nx;

        if( ep->dataType == UT_INT )
        {
            struct ValueI* da = &ep->data.i;
            da->val = da->min +
                      (ep->tx * (da->max - da->min) / ep->wid.area.w );
        }
        else
        {
            struct ValueF* da = &ep->data.f;
            da->val = da->min +
                      (ep->tx * (da->max - da->min) / ep->wid.area.w );
        }

        return 1;
    }
    return 0;
}


static void slider_dispatch( UThread* ut, GWidget* wp, const GLViewEvent* ev )
{
    EX_PTR;
    //printf( "KR button %d\n", ev->type );

    switch( ev->type )
    {
        case GLV_EVENT_BUTTON_DOWN:
            if( ev->code == GLV_BUTTON_LEFT )
            {
                //slider_setState( ut, ep, BTN_STATE_DOWN );
                ep->held = 1;
                gui_grabMouse( wp, 1 );
            }
            break;

        case GLV_EVENT_BUTTON_UP:
            if( ev->code == GLV_BUTTON_LEFT )
            {
                int pressed = (ep->state == BTN_STATE_DOWN);
                gui_ungrabMouse( wp );
                //slider_setState( ut, ep, BTN_STATE_UP );
                ep->held = 0;
                if( pressed && gui_widgetContains( wp, ev->x, ev->y ) )
                    goto activate;
            }
            break;

        case GLV_EVENT_MOTION:
            if( ep->held )
            {
                if( glEnv.mouseDeltaX )
                {
                    if( slider_adjust( ep, glEnv.mouseDeltaX ) )
                        goto trans;
                }
            }
            break;

        case GLV_EVENT_KEY_DOWN:
            if( ev->code == KEY_Left )
            {
                if( slider_adjust( ep, -2 ) )
                    goto trans;
            }
            else if( ev->code == KEY_Right )
            {
                if( slider_adjust( ep, 2 ) )
                    goto trans;
            }
            else if( ev->code == KEY_Home )
            {
                if( slider_adjust( ep, -9999 ) )
                    goto trans;
            }
            else if( ev->code == KEY_End )
            {
                if( slider_adjust( ep, 9999 ) )
                    goto trans;
            }
            // Fall through...

        case GLV_EVENT_KEY_UP:
            gui_ignoreEvent( ev );
            break;

        case GLV_EVENT_FOCUS_IN:
            break;

        case GLV_EVENT_FOCUS_OUT:
            //slider_setState( ut, ep, BTN_STATE_UP );
            break;
    }
    return;

trans:
    if( ep->dpTrans )
    {
        UIndex resN = gui_parentDrawProg( wp );
        if( resN != UR_INVALID_BUF )
            ur_setTransXY( ut, resN, ep->dpTrans, (float) ep->tx, 0.0f );
    }
activate:
    if( ep->actionN )
        gui_doBlockN( ut, ep->actionN );
}


static void slider_sizeHint( GWidget* wp, GSizeHint* size )
{
    UCell* rc;
    //EX_PTR;
    //UThread* ut = glEnv.guiUT;
    (void) wp;

    rc = glEnv.guiStyle + CI_STYLE_SLIDER_SIZE;
    if( ur_is(rc, UT_COORD) )
    {
        size->minW = rc->coord.n[0];
        size->minH = rc->coord.n[1];
    }
    else
    {
        size->minW = 20;
        size->minH = 20;
    }

    size->maxW    = GW_MAX_DIM;
    size->maxH    = size->minH;
    size->weightX = 2;
    size->weightY = 1;
    size->policyX = GW_EXPANDING;
    size->policyY = GW_FIXED;
}


static void slider_layout( GWidget* wp )
{
    UCell* rc;
    UCell* style = glEnv.guiStyle;
    UThread* ut = glEnv.guiUT;
    EX_PTR;
    //int horiz = (ep->orient == HSLIDER);

    if( ! gDPC )
        return;

    // Set draw list variables.

    rc = style + CI_STYLE_AREA;
    gui_initRectCoord( rc, wp, UR_ATOM_RECT );


    // Compile draw lists.

    //ep->dpSwitch = dp_beginSwitch( gDPC, 2 );

    rc = style + CI_STYLE_SLIDER_GROOVE;
    if( ur_is(rc, UT_BLOCK) )
        ur_compileDP( ut, rc, 1 );
    //dp_endCase( gDPC, ep->dpSwitch );

#if 1
    rc = style + CI_STYLE_SLIDER;
        //(horiz ? CI_STYLE_SLIDER_H : CI_STYLE_SLIDER_V);
    if( ur_is(rc, UT_BLOCK) )
    {
        ep->dpTrans = dp_beginTransXY( gDPC, ep->tx, 0.0f );
        ur_compileDP( ut, rc, 1 );
        dp_endTransXY( gDPC );
    }
    //dp_endCase( gDPC, ep->dpSwitch );
#endif

    //dp_endSwitch( gDPC, ep->dpSwitch, ep->state );
}


static int slider_select( GWidget* wp, UAtom atom, UCell* res )
{
    EX_PTR;
    if( atom == UR_ATOM_VALUE )
    {
        ur_setId(res, ep->dataType);
        if( ep->dataType == UT_INT )
            ur_int(res) = ep->data.i.val;
        else
            ur_decimal(res) = ep->data.f.val;
        return UR_OK;
    }
    return gui_areaSelect( wp, atom, res );
}


GWidgetClass wclass_slider =
{
    "slider",
    slider_make,        widget_free,        slider_mark,
    slider_dispatch,    slider_sizeHint,    slider_layout,
    widget_renderNul,   slider_select,
    0, 0
};


// EOF