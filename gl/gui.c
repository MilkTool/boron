/*
  Boron OpenGL GUI
  Copyright 2008-2011 Karl Robillard

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

/*
  TODO

  [X] Layout dialect, way to make widgets
     [X] Use 'parse or C code?
     [X] Need to support custom widget classes (make method parses layout)
  [X] class-specific data (inherited structure)
  [X] Rendering
     [X] styles
  [X] Layout
  [/] Input
  [ ] Handle GUIs in viewports (interfaces on objects in simulation world).
*/


#include "glh.h"
#include <GL/glv_keys.h>
#include "boron.h"
#include "boron-gl.h"
#include "gl_atoms.h"
#include "gui.h"
#include "os.h"
#include "draw_prog.h"


extern void block_markBuf( UThread* ut, UBuffer* buf );
extern int boron_doVoid( UThread* ut, const UCell* blkC );


//#define REPORT_LAYOUT   1

#define MAX_DIM     0x7fff


#define IS_ENABLED(wp)  (! (wp->flags & GW_DISABLED))

#define EACH_CHILD(parent,it)   for(it = parent->child; it; it = it->next) {

#define EACH_SHOWN_CHILD(parent,it) \
    EACH_CHILD(parent,it) \
        if( it->flags & GW_HIDDEN ) \
            continue;

#define EACH_END    }

#define MIN(x,y)    ((x) < (y) ? (x) : (y))

#define INIT_EVENT(ms,et,ec,es,ex,ey) \
    ms.type  = et; \
    ms.code  = ec; \
    ms.state = es; \
    ms.x     = ex; \
    ms.y     = ey


GWidget* gui_allocWidget( int size, const GWidgetClass* wclass )
{
    GWidget* wp = (GWidget*) memAlloc( size );
    if( wp )
    {
        memSet( wp, 0, size );
        wp->wclass = wclass;
        wp->flags  = GW_UPDATE_LAYOUT;
    }
    return wp;
}


void widget_free( GWidget* wp )
{
    memFree( wp );
}


static void _freeWidget( GWidget* wp )
{
    GWidget* it;
    GWidget* next;
    for( it = wp->child; it; it = next )
    {
        next = it->next;
        _freeWidget( it );
    }
    wp->wclass->free( wp );
}


static void gui_removeFocus( GWidget* );

void gui_freeWidget( GWidget* wp )
{
    if( wp )
    {
        gui_removeFocus( wp );
        _freeWidget( wp );
    }
}


/*
  Returns non-zero if widget contains point x,y.
*/
int gui_widgetContains( const GWidget* wp, int x, int y )
{
    if( (x < wp->area.x) ||
        (y < wp->area.y) ||
        (x >= (wp->area.x + wp->area.w)) ||
        (y >= (wp->area.y + wp->area.h)) )
        return 0;
    return 1;
}


/*
  Set cell to coord! holding widget area.
*/
void gui_initRectCoord( UCell* cell, GWidget* w, UAtom what )
{
    ur_setId(cell, UT_COORD);
    if( what == UR_ATOM_SIZE )
    {
        cell->coord.len = 2;
        cell->coord.n[0] = w->area.w;
        cell->coord.n[1] = w->area.h;
    }
    else if( what == UR_ATOM_POS )
    {
        cell->coord.len = 2;
        cell->coord.n[0] = w->area.x;
        cell->coord.n[1] = w->area.y;
    }
    else
    {
        cell->coord.len = 4;
        cell->coord.n[ 0 ] = w->area.x;
        cell->coord.n[ 1 ] = w->area.y;
        cell->coord.n[ 2 ] = w->area.w;
        cell->coord.n[ 3 ] = w->area.h;
    }
}


//----------------------------------------------------------------------------


#define no_mark     0
#define no_layout   widget_render
#define no_render   widget_render
#define no_dispatch widget_dispatch


void widget_dispatch( UThread* ut, GWidget* wp, const GLViewEvent* ev )
{
    (void) ut;
    (void) wp;
    (void) ev;
}


void widget_render( GWidget* wp )
{
    (void) wp;
}


int gui_areaSelect( GWidget* wp, UAtom atom, UCell* result )
{
    switch( atom )
    {
        case UR_ATOM_RECT:
        case UR_ATOM_SIZE:
        case UR_ATOM_POS:
            gui_initRectCoord( result, wp, atom );
            return 1;
    }
    ur_error( glEnv.guiUT, UR_ERR_SCRIPT, "Invalid widget selector '%s",
              ur_atomCStr(glEnv.guiUT, atom) );
    return 0;
}


#if 0
#define gui_areaChanged(wp,rect) \
    ((wp->area.x != rect->x) || (wp->area.y != rect->y) || \
     (wp->area.w != rect->w) || (wp->area.h != rect->h))

/*
  Returns non-zero if area changed.
*/
static int areaUpdate( GWidget* wp, GRect* rect )
{
    if( gui_areaChanged( wp, rect ) )
    {
        wp->area = *rect;
        return 1;
    }
    return 0;
}
#endif


//----------------------------------------------------------------------------


#define EXP_MIN     user


static GWidget* expand_make( UThread* ut, UBlockIter* bi,
                             const GWidgetClass* wclass )
{
    GWidget* wp;
    (void) ut;

    ++bi->it;
    wp = gui_allocWidget( sizeof(GWidget), wclass );

    if( bi->it != bi->end && ur_is(bi->it, UT_INT) )
    {
        // Does not handle input.
        wp->flags |= GW_DISABLED;
        wp->EXP_MIN = ur_int(bi->it);
        ++bi->it;
    }
    else
    {
        // Does not handle input or add spacing. 
        wp->flags |= GW_DISABLED | GW_NO_SPACE;
        wp->EXP_MIN = 0;
    }
    return wp;
}


static void expand_sizeHint( GWidget* wp, GSizeHint* size )
{
    size->minW    = size->minH    = wp->EXP_MIN;
    size->maxW    = size->maxH    = MAX_DIM;
    size->weightX = size->weightY = 1;
    size->policyX = size->policyY = GW_EXPANDING;
}


//----------------------------------------------------------------------------


typedef struct
{
    GWidget  wid;
    GWidget* keyFocus;
    GWidget* mouseFocus;
    char     mouseGrabbed;
}
GUIRoot;


static GWidget* root_make( UThread* ut, UBlockIter* bi,
                           const GWidgetClass* wclass )
{
    GUIRoot* ep;
    const UCell* arg = bi->it;


    ep = (GUIRoot*) gui_allocWidget( sizeof(GUIRoot), wclass );

    ep->keyFocus = ep->mouseFocus = 0;
    ep->mouseGrabbed = 0;


    if( ++arg == bi->end )
        goto arg_end;
    if( ur_is(arg, UT_BLOCK) )
    {
        if( ! gui_makeWidgets( ut, arg, (GWidget*) ep ) )
        {
            wclass->free( (GWidget*) ep );
            return 0;
        }
        if( ep->wid.child )
            ep->keyFocus = ep->wid.child;
        ++arg;
    }

arg_end:

    bi->it = arg;
    return (GWidget*) ep;
}


GWidgetClass wclass_root;

static void gui_removeFocus( GWidget* wp )
{
    GUIRoot* ui = (GUIRoot*) gui_root( wp );
    if( ui->wid.wclass == &wclass_root )
    {
        if( ui->mouseFocus == wp )
        {
            ui->mouseFocus = 0;
            if( ui->mouseGrabbed )
                ui->mouseGrabbed = 0;
        }
        if( ui->keyFocus == wp )
            ui->keyFocus = 0;
    }
}


static void sendFocusEvent( GWidget* wp, int eventType )
{
    if( wp && IS_ENABLED(wp) )
    {
        GLViewEvent me;
        INIT_EVENT( me, eventType, 0, 0, 0, 0 );
        wp->wclass->dispatch( glEnv.guiUT, wp, &me );
    }
}


static void gui_setMouseFocus( GUIRoot* ui, GWidget* wp )
{
    if( ui->mouseFocus != wp )
    {
        sendFocusEvent( ui->mouseFocus, GLV_EVENT_FOCUS_OUT );
        ui->mouseFocus = wp;
        sendFocusEvent( wp, GLV_EVENT_FOCUS_IN );
    }
}


void gui_setKeyFocus( GWidget* wp )
{
    GUIRoot* ui = (GUIRoot*) gui_root( wp );
    if( ui->wid.wclass == &wclass_root )
    {
        ui->keyFocus = wp;
    }
}


void gui_grabMouse( GWidget* wp, int keyFocus )
{
    GUIRoot* ui = (GUIRoot*) gui_root( wp );
    if( ui->wid.wclass == &wclass_root )
    {
        ui->mouseFocus = wp;
        ui->mouseGrabbed = 1;
        if( keyFocus )
            ui->keyFocus = wp;
    }
}


void gui_ungrabMouse( GWidget* wp )
{
    GUIRoot* ui = (GUIRoot*) gui_root( wp );
    if( ui->wid.wclass == &wclass_root )
    {
        if( ui->mouseFocus == wp )
            ui->mouseGrabbed = 0;
    }
}


int gui_hasFocus( GWidget* wp )
{
    GUIRoot* ui = (GUIRoot*) gui_root( wp );
    int mask = 0;
    if( ui->wid.wclass == &wclass_root )
    {
        if( ui->keyFocus == wp )
            mask |= GW_FOCUS_KEY;
        if( ui->mouseFocus == wp )
        {
            mask |= GW_FOCUS_MOUSE;
            if( ui->mouseGrabbed )
                mask |= GW_FOCUS_GRAB;
        }
    }
    return mask;
}


/*
  Returns child of wp under event x,y, or zero if no child contains the point.
*/
static GWidget* childAt( GWidget* wp, const GLViewEvent* ev )
{
    GWidget* it;
    EACH_SHOWN_CHILD( wp, it )
        if( gui_widgetContains( it, ev->x, ev->y ) )
        {
            wp = childAt( it, ev );
            return wp ? wp : it;
        }
    EACH_END
    return 0;
}


static void root_dispatch( UThread* ut, GWidget* wp, const GLViewEvent* ev )
{
    GWidget* cw;
    GUIRoot* ep = (GUIRoot*) wp;

#if 0
    printf( "KR dispatch %d  mouseFocus %d  keyFocus %d\n",
            ev->type, ep->mouseFocus, ep->keyFocus );
#endif

    switch( ev->type )
    {
        case GLV_EVENT_RESIZE:
            if( (ev->x != wp->area.w) || (ev->y != wp->area.h) )
            {
                wp->flags |= GW_UPDATE_LAYOUT;
                wp->area.w = ev->x;
                wp->area.h = ev->y;
            }
            break;

        case GLV_EVENT_CLOSE:
            if( (cw = wp->child) )
                goto dispatch;
            /*
            boron_throwWord( ut, UR_ATOM_QUIT );
            UR_GUI_THROW;   // Ignores any later events.
            */
            return;

        case GLV_EVENT_BUTTON_DOWN:
        case GLV_EVENT_BUTTON_UP:
        case GLV_EVENT_WHEEL:
            if( (cw = ep->mouseFocus) )
                goto dispatch;
            return;

        case GLV_EVENT_MOTION:
            if( (cw = ep->mouseFocus) )
            {
                if( ep->mouseGrabbed )
                    goto dispatch;
                if( gui_widgetContains( cw, ev->x, ev->y ) )
                {
                    GWidget* cw2 = childAt( cw, ev );
                    if( cw2 )
                    {
                        gui_setMouseFocus( ep, cw2 );
                        cw = cw2;
                    }
                    goto dispatch;
                }
            }

            cw = childAt( wp, ev );
            gui_setMouseFocus( ep, cw );
            if( cw )
                goto dispatch;
            break;

        case GLV_EVENT_KEY_DOWN:
        case GLV_EVENT_KEY_UP:
            if( (cw = ep->keyFocus) )
                goto dispatch;
            break;

        case GLV_EVENT_FOCUS_IN:
            ep->mouseFocus = childAt( wp, ev );
            // Fall through...

        case GLV_EVENT_FOCUS_OUT:
            cw = ep->mouseFocus;
            if( cw )
                goto dispatch;
            break;
    }
    return;

dispatch:

    cw->wclass->dispatch( ut, cw, ev );
}


static void root_sizeHint( GWidget* wp, GSizeHint* size )
{
    size->minW    =
    size->maxW    = wp->area.w;
    size->minH    =
    size->maxH    = wp->area.h;
    size->weightX =
    size->weightY = 0;
    size->policyX =
    size->policyY = GW_FIXED;
}


static void root_render( GWidget* wp )
{
    GWidget* it;
    //GLViewEvent me;

    if( ! (glEnv.guiStyle = gui_style( glEnv.guiUT )) )
        return;

    if( wp->flags & GW_UPDATE_LAYOUT )
    {
        wp->flags &= ~GW_UPDATE_LAYOUT;

        /*
        INIT_EVENT( me, GLV_EVENT_RESIZE, 0, 0, wp->area.w, wp->area.h );
        wc->dispatch( glEnv.guiUT, wp, &me );
        wc->layout( glEnv.guiUT, wp );
        */
    }

    EACH_SHOWN_CHILD( wp, it )
        it->wclass->render( it );
    EACH_END
}


//----------------------------------------------------------------------------


#define BOX_MEMBERS \
    uint16_t marginL; \
    uint16_t marginT; \
    uint16_t marginR; \
    uint16_t marginB; \
    uint16_t spacing; \
    uint8_t  rows; \
    uint8_t  cols;

typedef struct
{
    GWidget wid;
    BOX_MEMBERS
}
Box;

#define EX_PTR  Box* ep = (Box*) wp


/*
  mc must be a coord!
*/
static void setBoxMargins( Box* wd, const UCell* mc )
{
    wd->marginL = mc->coord.n[0];
    wd->marginT = mc->coord.n[1];

    if( mc->coord.len < 4 )
    {
        wd->marginR = wd->marginL;
        wd->marginB = wd->marginT;
        if( mc->coord.len == 3 )
            wd->spacing = mc->coord.n[2];
    }
    else
    {
        wd->marginR = mc->coord.n[2];
        wd->marginB = mc->coord.n[3];
        if( mc->coord.len == 5 )
            wd->spacing = mc->coord.n[4];
    }
}


// box [margin coord!] block
static GWidget* box_make( UThread* ut, UBlockIter* bi,
                          const GWidgetClass* wclass )
{
    Box* ep;
    const UCell* arg = bi->it;


    ep = (Box*) gui_allocWidget( sizeof(Box), wclass );

    ep->wid.flags |= GW_DISABLED;   // Does not handle input. 

    ep->marginL = 0;
    ep->marginT = 0;
    ep->marginR = 0;
    ep->marginB = 0;
    ep->spacing = 8;


    if( ++arg == bi->end )
        goto arg_end;
    if( ur_is(arg, UT_COORD) )
    {
        setBoxMargins( ep, arg );
        if( ++arg == bi->end )
            goto arg_end;
    }

    if( ur_is(arg, UT_BLOCK) )
    {
        if( ! gui_makeWidgets( ut, arg, (GWidget*) ep ) )
        {
            wclass->free( (GWidget*) ep );
            return 0;
        }
        ++arg;
    }

arg_end:

    bi->it = arg;
    return (GWidget*) ep;
}


static void box_mark( UThread* ut, GWidget* wp )
{
    GMarkFunc func;
    GWidget* it;

    EACH_CHILD( wp, it )
        if( (func = it->wclass->mark) )
            func( ut, it );
    EACH_END
}


static void hbox_sizeHint( GWidget* wp, GSizeHint* size )
{
    EX_PTR;
    GWidget* it;
    GSizeHint cs;
    int count = 0;
    int marginH = ep->marginT + ep->marginB;

    size->minW    = ep->marginL + ep->marginR;
    size->minH    = marginH;
    size->maxW    = MAX_DIM;
    size->maxH    = MAX_DIM;
    size->weightX = 2;
    size->weightY = 2;
    size->policyX = GW_FIXED;
    size->policyY = GW_FIXED;

    EACH_SHOWN_CHILD( wp, it )
        it->wclass->sizeHint( it, &cs );
        if( ! (it->flags & GW_NO_SPACE) )   // cs.minW != 0
            ++count;
        size->minW += cs.minW;
        cs.minH += marginH;
        if( size->minH < cs.minH )
            size->minH = cs.minH;
    EACH_END

    if( count > 1 )
        size->minW += (count - 1) * ep->spacing;
}


#define MAX_LO_WIDGETS 16

typedef struct
{
    int count;
    int spaceCount;
    int required;
    int widgetWeight;
    int expandWeight;
    GSizeHint hint[ MAX_LO_WIDGETS ];
}
LayoutData;


static void layout_query( GWidget* wp, LayoutData* lo )
{
    GWidget* it;
    GSizeHint* hint = lo->hint;

    lo->count = lo->spaceCount = 0;

    EACH_SHOWN_CHILD( wp, it )
        assert( lo->count < MAX_LO_WIDGETS );
        it->wclass->sizeHint( it, hint );
        if( ! (it->flags & GW_NO_SPACE) )
            ++lo->spaceCount;
        ++lo->count;
        ++hint;
    EACH_END
}


static void layout_stats( LayoutData* lo, int axis )
{
    int used = 0;
    int weight = 0;
    int ew = 0;
    GSizeHint* it  = lo->hint;
    GSizeHint* end = it + lo->count;
    if( axis == 'y' )
    {
        while( it != end )
        {
            if( it->policyY == GW_EXPANDING )
                ew += it->weightY;
            else
                weight += it->weightY;
            used += it->minH;
            ++it;
        }
    }
    else
    {
        while( it != end )
        {
            if( it->policyX == GW_EXPANDING )
                ew += it->weightX;
            else
                weight += it->weightX;
            used += it->minW;
            ++it;
        }
    }
    lo->required = used;
    lo->widgetWeight = weight;
    lo->expandWeight = ew;
}


static void hbox_layout( GWidget* wp /*, GRect* rect*/ )
{
    EX_PTR;
    GWidget* it;
    GSizeHint* hint;
    GSizeHint* hend;
    LayoutData lo;
    GRect cr;
    int dim;
    int room;

    layout_query( wp, &lo );
    layout_stats( &lo, 'x' );

    room = wp->area.w - ep->marginL - ep->marginR -
           (ep->spacing * (lo.spaceCount - 1));
    if( room > lo.required )
    {
        //int excess = room - lo.required;
        int avail = room;
        hint = lo.hint;
        hend = hint + lo.count;
        while( hint != hend )
        {
            if( hint->policyX == GW_WEIGHTED )
            {
                dim = (room * hint->weightX) / lo.widgetWeight;
                if( dim > hint->maxW )
                    hint->minW = hint->maxW;
                else if( dim > hint->minW )
                    hint->minW = dim;
                avail -= hint->minW;
            }
            else if( hint->policyX == GW_FIXED )
            {
                avail -= hint->minW;
            }
            ++hint;
        }

        if( avail > 0 && lo.expandWeight )
        {
            // Allocate unused space to expanders.
            hint = lo.hint;
            while( hint != hend )
            {
                if( hint->policyX == GW_EXPANDING )
                {
                    hint->minW = (avail * hint->weightX) / lo.expandWeight;
                }
                ++hint;
            }
        }
    }

    //gui_setArea( wp, rect );

    dim = wp->area.x + ep->marginL;
    room = wp->area.h - ep->marginT - ep->marginB;
    hint = lo.hint;
    EACH_SHOWN_CHILD( wp, it )
        cr.x = dim;
        cr.y = wp->area.y + ep->marginB;
        cr.w = hint->minW;
        cr.h = MIN( hint->maxH, room );

        dim += cr.w;
        if( ! (it->flags & GW_NO_SPACE) )
            dim += ep->spacing;

#ifdef REPORT_LAYOUT
        printf( "KR hbox layout  id %d  class %d  %d,%d,%d,%d\n",
                it.id, it->classId, cr.x, cr.y, cr.w, cr.h );
#endif
#if 1
        // Always call layout to recompile DL.
        it->area = cr;
        it->wclass->layout( it );
#else
        if( areaUpdate( it, &cr ) )
            it->wclass->layout( it );
#endif
        ++hint;
    EACH_END
}


static void box_render( GWidget* wp )
{
    GWidget* it;

    EACH_SHOWN_CHILD( wp, it )
        it->wclass->render( it );
    EACH_END
}


//----------------------------------------------------------------------------


static void vbox_sizeHint( GWidget* wp, GSizeHint* size )
{
    EX_PTR;
    GWidget* it;
    GSizeHint cs;
    int count = 0;
    int marginW = ep->marginL + ep->marginR;

    size->minW    = marginW;
    size->minH    = ep->marginT + ep->marginB;
    size->maxW    = MAX_DIM;
    size->maxH    = MAX_DIM;
    size->weightX = 2;
    size->weightY = 2;
    size->policyX = GW_WEIGHTED;
    size->policyY = GW_WEIGHTED;

    EACH_SHOWN_CHILD( wp, it )
        it->wclass->sizeHint( it, &cs );
        if( ! (it->flags & GW_NO_SPACE) )   // cs.minH != 0
            ++count;
        size->minH += cs.minH;
        cs.minW += marginW;
        if( size->minW < cs.minW )
            size->minW = cs.minW;
    EACH_END

    if( count > 1 )
        size->minH += (count - 1) * ep->spacing;
}


static void vbox_layout( GWidget* wp /*, GRect* rect*/ )
{
    EX_PTR;
    GWidget* it;
    GSizeHint* hint;
    GSizeHint* hend;
    LayoutData lo;
    GRect cr;
    int dim;
    int room;

    layout_query( wp, &lo );
    layout_stats( &lo, 'y' );

    room = wp->area.h - ep->marginT - ep->marginB -
           (ep->spacing * (lo.spaceCount - 1));
    if( room > lo.required )
    {
        //int excess = room - lo.required;
        int avail = room;
        hint = lo.hint;
        hend = hint + lo.count;
        while( hint != hend )
        {
            if( hint->policyY == GW_WEIGHTED )
            {
                dim = (room * hint->weightY) / lo.widgetWeight;
                if( dim > hint->maxH )
                    hint->minH = hint->maxH;
                else if( dim > hint->minH )
                    hint->minH = dim;
                avail -= hint->minH;
            }
            else if( hint->policyY == GW_FIXED )
            {
                avail -= hint->minH;
            }
            ++hint;
        }

        if( avail > 0 && lo.expandWeight )
        {
            // Allocate unused space to expanders.
            hint = lo.hint;
            while( hint != hend )
            {
                if( hint->policyY == GW_EXPANDING )
                {
                    hint->minH = (avail * hint->weightY) / lo.expandWeight;
                }
                ++hint;
            }
        }
    }

    //gui_setArea( wp, rect );

    dim = wp->area.y + wp->area.h - ep->marginT;
    room = wp->area.w - ep->marginL - ep->marginR;
    hint = lo.hint;
    EACH_SHOWN_CHILD( wp, it )
        cr.x = wp->area.x + ep->marginL;
        cr.w = MIN( hint->maxW, room );
        cr.h = hint->minH;
        cr.y = dim - cr.h;

        dim -= cr.h;
        if( ! (it->flags & GW_NO_SPACE) )
            dim -= ep->spacing;

#ifdef REPORT_LAYOUT
        printf( "KR vbox layout  id %d  class %d  %d,%d,%d,%d\n",
                it.id, it->classId, cr.x, cr.y, cr.w, cr.h );
#endif
#if 1
        // Always call layout to recompile DL.
        it->area = cr;
        it->wclass->layout( it );
#else
        if( areaUpdate( it, &cr ) )
            it->wclass->layout( it );
#endif
        ++hint;
    EACH_END
}


//----------------------------------------------------------------------------


typedef struct
{
    GWidget wid;
    BOX_MEMBERS
    UIndex   dp[2];
    UIndex   titleN;
    UIndex   eventN;        // Event handler block
}
GWindow;

#undef EX_PTR
#define EX_PTR  GWindow* ep = (GWindow*) wp

#define WINDOW_HBOX     GW_FLAG_USER1


static GWidget* window_make( UThread* ut, UBlockIter* bi,
                             const GWidgetClass* wclass )
{
    GWindow* ep;
    const UCell* arg = bi->it;

    ep = (GWindow*) gui_allocWidget( sizeof(GWindow), wclass );

    ep->marginL = 8;
    ep->marginT = 8;
    ep->marginR = 8;
    ep->marginB = 8;
    ep->spacing = 0;

    ep->dp[0] = ur_makeDrawProg( ut );


    /* TODO: Support path?
    if( ur_sel(arg) == UR_ATOM_HBOX )
        wp->flags |= WINDOW_HBOX;
    */

    if( ++arg == bi->end )
        goto arg_end;
    if( ur_is(arg, UT_STRING) )
    {
        ep->titleN = arg->series.buf;
        glv_setTitle( glEnv.view, boron_cstr( ut, arg, 0 ) );
    }

    if( ++arg == bi->end )
        goto arg_end;
    if( ur_is(arg, UT_BLOCK) )
    {
        ep->eventN = arg->series.buf;
    }

    if( ++arg == bi->end )
        goto arg_end;
    if( ur_is(arg, UT_BLOCK) )
    {
        if( ! gui_makeWidgets( ut, arg, (GWidget*) ep ) )
        {
            wclass->free( (GWidget*) ep );
            return 0;
        }
        ++arg;
    }

arg_end:

    bi->it = arg;
    return (GWidget*) ep;
}


static void window_mark( UThread* ut, GWidget* wp )
{
    EX_PTR;

    ur_markBuffer( ut, ep->dp[0] );
    if( ep->titleN > UR_INVALID_BUF )   // Also acts as (! ur_isShared(n))
        ur_markBuffer( ut, ep->titleN );
    ur_markBlkN( ut, ep->eventN );

    box_mark( ut, wp );
}


static void window_dispatch( UThread* ut, GWidget* wp, const GLViewEvent* ev )
{
    EX_PTR;
    UBlockIter bi;
    UAtom name;

    //printf( "KR window event %d\n", ev->type );
    if( ep->eventN )
    {
        switch( ev->type )
        {
            case GLV_EVENT_CLOSE:
                name = UR_ATOM_CLOSE;
                break;
            case GLV_EVENT_RESIZE:
                name = UR_ATOM_RESIZE;
                // TODO: Pass area to handler.
                break;
            default:
                return;
        }

        bi.buf = ur_buffer( ep->eventN );
        bi.it  = bi.buf->ptr.cell;
        bi.end = bi.it + bi.buf->used;
        if( bi.buf->used & 1 )
            --bi.end;
        while( bi.it != bi.end )
        {
            if( ur_is(bi.it, UT_WORD) )
            {
                if( ur_atom(bi.it) == name )
                {
                    ++bi.it;
                    if( ur_is(bi.it, UT_BLOCK) )
                    {
                        if( ! boron_doVoid( ut, bi.it ) )
                        {
                            UR_GUI_THROW;
                        }
                    }
                    return;
                }
            }
            bi.it += 2;
        }
    }
}


static void window_sizeHint( GWidget* wp, GSizeHint* size )
{
    if( wp->flags & WINDOW_HBOX )
        hbox_sizeHint( wp, size );
    else
        vbox_sizeHint( wp, size );
}


static void window_layout( GWidget* wp )
{
    GSizeHint hint;
    EX_PTR;
    DPCompiler* save;
    DPCompiler dpc;
    UCell* rc;
    UCell* style = glEnv.guiStyle;
    UThread* ut = glEnv.guiUT;


    save = ur_beginDP( &dpc );

    rc = style + CI_STYLE_START_DL;
    if( ur_is(rc, UT_BLOCK) )
        ur_compileDP( ut, rc, 1 );


    rc = style + CI_STYLE_WINDOW_MARGIN;
    if( ur_is(rc, UT_COORD) )
        setBoxMargins( (Box*) ep, rc );

    window_sizeHint( wp, &hint );
    if( wp->area.w < hint.minW )
        wp->area.w = hint.minW;
    if( wp->area.h < hint.minH )
        wp->area.h = hint.minH;

    // Set draw list variables.
    rc = style + CI_STYLE_LABEL;
    ur_setId( rc, UT_STRING );
    ur_setSeries( rc, ep->titleN, 0 );

    rc = style + CI_STYLE_AREA;
    gui_initRectCoord( rc, wp, UR_ATOM_RECT );

    // Compile draw list.

    rc = style + CI_STYLE_WINDOW;
    if( ur_is(rc, UT_BLOCK) )
        ur_compileDP( ut, rc, 1 );

    if( wp->flags & WINDOW_HBOX )
        hbox_layout( wp );
    else
        vbox_layout( wp );

    ur_endDP( ut, ur_buffer(ep->dp[0]), save );
}


static void window_render( GWidget* wp )
{
    EX_PTR;
    DPState ds;


    if( ! (glEnv.guiStyle = gui_style( glEnv.guiUT )) )
        return;

    if( wp->flags & GW_UPDATE_LAYOUT )
    {
        wp->flags &= ~GW_UPDATE_LAYOUT;
        window_layout( wp );
    }

    ur_initDrawState( &ds );
    ur_runDrawProg( glEnv.guiUT, &ds, ep->dp[0] );

    box_render( wp );
}


//----------------------------------------------------------------------------


GWidgetClass wclass_root =
{
    "root",
    root_make,          widget_free,        box_mark,
    root_dispatch,      root_sizeHint,      no_layout,
    root_render,        gui_areaSelect,
    0, 0
};


GWidgetClass wclass_hbox =
{
    "hbox",
    box_make,           widget_free,        box_mark,
    no_dispatch,        hbox_sizeHint,      hbox_layout,
    box_render,         gui_areaSelect,
    0, 0
};


GWidgetClass wclass_vbox =
{
    "vbox",
    box_make,           widget_free,        box_mark,
    no_dispatch,        vbox_sizeHint,      vbox_layout,
    box_render,         gui_areaSelect,
    0, 0
};


GWidgetClass wclass_window =
{
    "window",
    window_make,        widget_free,        window_mark,
    window_dispatch,    window_sizeHint,    window_layout,
    window_render,      gui_areaSelect,
    0, 0
};


GWidgetClass wclass_expand =
{
    "expand",
    expand_make,        widget_free,        no_mark,
    no_dispatch,        expand_sizeHint,    no_layout,
    no_render,          gui_areaSelect,
    0, 0
};


extern GWidgetClass wclass_script;
extern GWidgetClass wclass_button;
extern GWidgetClass wclass_checkbox;
extern GWidgetClass wclass_label;
extern GWidgetClass wclass_lineedit;
extern GWidgetClass wclass_list;
/*
    "console",
    "option",
    "choice",
    "menu",
    "data",         // label
    "data-edit",    // spin box, line editor
*/

void gui_addStdClasses()
{
    GWidgetClass* classes[ 11 ];

    classes[0]  = &wclass_root;
    classes[1]  = &wclass_expand;
    classes[2]  = &wclass_hbox;
    classes[3]  = &wclass_vbox;
    classes[4]  = &wclass_window;
    classes[5]  = &wclass_script;
    classes[6]  = &wclass_button;
    classes[7]  = &wclass_checkbox;
    classes[8]  = &wclass_label;
    classes[9]  = &wclass_lineedit;
    classes[10] = &wclass_list;

    gui_addWidgetClasses( classes, 11 );
}


//----------------------------------------------------------------------------


/*
  Return draw-prog buffer index or UR_INVALID_BUF.
*/
UIndex gui_parentDrawProg( GWidget* wp )
{
    while( wp->parent )
    {
        wp = wp->parent;
        if( wp->wclass == &wclass_window )
            return ((GWindow*) wp)->dp[0];
    }
    return UR_INVALID_BUF;
}


#if 0
/*
  The select method returns non-zero if result is set, or zero if error.
  The select method must not throw errors.
*/
int gui_selectAtom( GWidget* wp, UAtom atom, UCell* result )
{
    return wp->wclass->select( wp, atom, result );
}
#endif


/*
  Do block, report any exception, and restore stacks.
*/
void gui_doBlock( UThread* ut, const UCell* blkC )
{
    /*
    UBuffer* buf = ur_errorBlock(ut);
    UIndex errUsed = buf->used;
    */

    if( ! boron_doBlock( ut, blkC, boron_result(ut) ) )
    {
#if 1
        UR_GUI_THROW;
#else
        // Report any exception, and restore stacks.
        UBuffer* buf;
        UCell* ex = boron_exception( ut );
        if( ur_is(ex, UT_ERROR) )
        {
            UBuffer* str = &glEnv.tmpStr;
            str->used = 0;
            ur_toText( ut, ex, str );
            ur_strTermNull( str );
            fprintf( stderr, str->ptr.c );
        }

        buf = ur_errorBlock(ut);
        buf->used = 0;
#endif
    }
}


void gui_doBlockN( UThread* ut, UIndex blkN )
{
    if( ! boron_doBlockN( ut, blkN, boron_result(ut) ) )
    {
        UR_GUI_THROW;
    }
}


/*
  Append child to end of parent list.
  Child must be valid and unlinked.
*/
void gui_appendChild( GWidget* parent, GWidget* child )
{
    GWidget* it = parent->child;
    if( it )
    {
        while( it->next )
            it = it->next;
        it->next = child; 
    }
    else
    {
        parent->child = child;
    }
    child->parent = parent;
}


/*
  Remove widget from parent list.
*/
void gui_unlink( GWidget* wp )
{
    GWidget* it;
    GWidget* prev;
    GWidget* parent = wp->parent;
    if( parent )
    {
        wp->parent = 0;
        prev = 0;
        for( it = parent->child; it; it = it->next )
        {
            if( it == wp )
            {
                if( prev )
                    prev->next = wp->next;
                else
                    parent->child = wp->next;
                break;
            }
        }
    }
}


/**
  Returns pointer to root parent widget.
*/
GWidget* gui_root( GWidget* wp )
{
    while( wp->parent )
        wp = wp->parent;
    return wp;
}


void gui_enable( GWidget* wp, int active )
{
    int disabled = wp->flags & GW_DISABLED;
    if( active )
    {
        if( disabled )
            wp->flags &= ~GW_DISABLED;
    }
    else if( ! disabled )
    {
        wp->flags |= GW_DISABLED;
    }
}


static void markLayoutDirty( GWidget* wp )
{
    wp->flags |= GW_UPDATE_LAYOUT;
    if( wp->parent )
        markLayoutDirty( wp->parent );
}


void gui_show( GWidget* wp, int show )
{
    int hidden = wp->flags & GW_HIDDEN;
    if( show )
    {
        if( hidden )
        {
            wp->flags &= ~GW_HIDDEN;
            if( wp->parent )
                markLayoutDirty( wp->parent );
        }
    }
    else if( ! hidden )
    {
        gui_removeFocus( wp );
        wp->flags |= GW_HIDDEN;
        if( wp->parent )
            markLayoutDirty( wp->parent );
    }
}


UCell* gui_style( UThread* ut )
{
    UBuffer* ctx = ur_threadContext( ut );
    int n = ur_ctxLookup( ur_ctxSort(ctx), UR_ATOM_GUI_STYLE );
    if( n > -1 )
    {
        UCell* cc = ur_ctxCell( ctx, n );
        if( ur_is(cc, UT_CONTEXT) )
        {
            ctx = ur_bufferSerM(cc);
            return ur_ctxCell( ctx, 0 );
        }
    }
    return 0;
}


void gui_addWidgetClasses( GWidgetClass** classTable, int count )
{
    UBuffer* ctx = &glEnv.widgetClasses;
    UBuffer atoms;
    int i;
    int index;

    {
    UBuffer* str = &glEnv.tmpStr;
    str->used = 0;
    ur_arrInit( &atoms, sizeof(UAtom), count );
    for( i = 0; i < count; ++i )
    {
        if( i )
            ur_strAppendChar( str, ' ' );
        ur_strAppendCStr( str, classTable[ i ]->name );
    }
    ur_strTermNull( str );
    ur_internAtoms( glEnv.guiUT, str->ptr.c, atoms.ptr.u16 );
    }

    ur_ctxReserve( ctx, ctx->used + count );
    for( i = 0; i < count; ++i )
    {
        GWidgetClass* wc = *classTable++;
        wc->nameAtom = atoms.ptr.u16[ i ];
        index = ur_ctxAddWordI( ctx, wc->nameAtom );
        ((GWidgetClass**) ctx->ptr.v)[ index ] = wc;
    }
    ur_ctxSort( ctx );

    ur_arrFree( &atoms );
}


GWidgetClass* gui_widgetClass( UAtom name )
{
    int i = ur_ctxLookup( &glEnv.widgetClasses, name );
    if( i < 0 )
        return 0;
    return ((GWidgetClass**) glEnv.widgetClasses.ptr.v)[ i ];
}


#if 0
/*.cf.
   make-widget
        parent  widget!/none!
        block   block!
*/
CFUNC_PUB( make_widget )
{
    GWidget* parent = 0;
    if( ur_is(a1, UT_WIDGET) )
        parent = gui_widgetPtr( a1 );
    return ur_error( ut, UR_ERR_SCRIPT, "widget! make expected block!" );
}
#endif


static void ur_arrAppendPtr( UBuffer* arr, void* ptr )
{
    ur_arrReserve( arr, arr->used + 1 );
    ((void**) arr->ptr.v)[ arr->used ] = ptr;
    ++arr->used;
}


/*
  \param blkC       Valid block with widget layout language.
  \param parent     Pointer to parent or zero if none.

  \return UR_OK/UR_THROW
*/
int gui_makeWidgets( UThread* ut, const UCell* blkC, GWidget* parent )
{
    UBlockIter bi;
    GWidgetClass* wclass;
    GWidget* wp;
    const UCell* cell;
    const UCell* setWord = 0;


    ur_blkSlice( ut, &bi, blkC );
    while( bi.it != bi.end )
    {
        if( ur_is(bi.it, UT_SETWORD) )
        {
            setWord = bi.it++;
        }
        else if( ur_is(bi.it, UT_WORD) )
        {
            wclass = gui_widgetClass( ur_atom(bi.it) );
            if( ! wclass )
            {
                return ur_error( ut, UR_ERR_SCRIPT, "unknown widget class '%s",
                                 ur_wordCStr( bi.it ) );
            }
            wp = wclass->make( ut, &bi, wclass );
            if( ! wp )
                return UR_THROW;

            if( parent )
            {
                gui_appendChild( parent, wp );
            }
            else
            {
                ur_arrAppendPtr( &glEnv.rootWidgets, wp );
            }

            if( setWord )
            {
                if( ! (cell = ur_wordCellM( ut, setWord )) )
                    return UR_THROW;
                ur_setId(cell, UT_WIDGET);
                ur_widgetPtr(cell) = wp;
                setWord = 0;
            }
        }
        else
        {
            return ur_error( ut, UR_ERR_TYPE,
                             "widget make expected name word! or set-word!" );
        }
    }
    return UR_OK;
}


#ifdef DEBUG
static void dumpIndent( int indent )
{
    while( indent-- )
        printf( "    " );
}


void gui_dumpWidget( const GWidget* wp, int indent )
{
    dumpIndent( indent );
    printf( "%p %s %d,%d,%d,%d 0x%02X", wp, wp->wclass->name,
            wp->area.x, wp->area.y, wp->area.w, wp->area.h, wp->flags );
    if( wp->child )
    {
        GWidget* it;
        printf( " [\n" );
        ++indent;
        EACH_CHILD( wp, it )
            gui_dumpWidget( it, indent );
        EACH_END
        --indent;
        dumpIndent( indent );
        printf( "]\n" );
    }
    else
    {
        printf( "\n" );
    }
}
#endif


// EOF
