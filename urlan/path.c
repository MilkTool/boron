/*
  Copyright 2009,2012 Karl Robillard

  This file is part of the Urlan datatype system.

  Urlan is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Urlan is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with Urlan.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "urlan.h"


/** \defgroup dt_path Datatype Path
  \ingroup urlan
  @{
*/ 


/**
  Get a pointer to the last value that a path! refers to.

  If the last path node is not a datatype that contains UCells then lastCell
  will be set to tmp.

  \param pi         Path iterator.
  \param tmp        Cell to temporarily hold values for datatypes that don't
                    contain UCells.
  \param lastCell   Set to the address of the cell holding the last value in
                    the path or tmp.

  \return Type of the first node (UT_WORD or UT_GETWORD), or UR_THROW if
          path evaluation fails.

  \sa ur_pathCell, ur_wordCell
*/
int ur_pathResolve( UThread* ut, UBlockIt* pi, UCell* tmp, UCell** lastCell )
{
    const UCell* obj = 0;
    const UCell* selector;
    const UCell* (*selectf)( UThread*, const UCell*, const UCell*, UCell* );
    int type;

    if( pi->it == pi->end )
    {
bad_word:
        return ur_error( ut, UR_ERR_SCRIPT,
                         "Path must start with a word!/get-word!");
    }

    type = ur_type(pi->it);
    if( type != UT_WORD && type != UT_GETWORD )
        goto bad_word;

    if( ! (obj = ur_wordCell( ut, pi->it )) )
        return UR_THROW;
    if( ur_is(obj, UT_UNSET) )
    {
        return ur_error( ut, UR_ERR_SCRIPT, "Path word '%s is unset",
                         ur_wordCStr( pi->it ) );
    }

    while( ++pi->it != pi->end )
    {
        // If the select method is NULL, return obj as the result and leave
        // pi->it pointing to the untraversed path segments.
        selectf = ut->types[ ur_type(obj) ]->select;
        if( ! selectf )
            break;

        selector = pi->it;
        if( ur_is(selector, UT_GETWORD) )
        {
            if( ! (selector = ur_wordCell( ut, selector )) )
                return UR_THROW;
        }

        obj = selectf( ut, obj, selector, tmp );
        if( ! obj )
            return UR_THROW;
    }

    *lastCell = (UCell*) obj;
    return type;
}


/**
  Get the value which a path refers to.

  \param path   Valid UT_PATH cell.
  \param res    Set to value at end of path.

  \return UT_WORD/UT_GETWORD/UR_THROW

  \sa ur_pathResolve, ur_wordCell
*/
int ur_pathCell( UThread* ut, const UCell* path, UCell* res )
{
    UBlockIt bi;
    UCell* last;
    int wordType;

    ur_blockIt( ut, &bi, path );
    if( ! (wordType = ur_pathResolve( ut, &bi, res, &last )) )
        return UR_THROW;
    if( last != res )
        *res = *last;
    return wordType;
}


/*
  Returns zero if word not found.
*/
static UCell* ur_blkSelectWord( UBuffer* buf, UAtom atom )
{
    // Should this work on cell and use UBlockIter?
    UCell* it  = buf->ptr.cell;
    UCell* end = it + buf->used;
    while( it != end )
    {
        // Checking atom first would be faster (it will fail more often and
        // is a quicker check), but the atom field may not be intialized
        // memory so memory checkers will report an error.
        if( ur_isWordType( ur_type(it) ) && (ur_atom(it) == atom) )
        {
            if( ++it == end )
                return 0;
            return it;
        }
        ++it;
    }
    return 0;
}


extern int coord_poke( UThread*, UCell* cell, int index, const UCell* src );
extern int vec3_poke ( UThread*, UCell* cell, int index, const UCell* src );

/**
  Set path.  This copies src into the cell which the path refers to.

  If any of the path words are unbound (or bound to the shared environment)
  then an error is generated and UR_THROW is returned.

  \param path   Valid path cell.
  \param src    Source value to copy.

  \return UR_OK/UR_THROW
*/
UStatus ur_setPath( UThread* ut, const UCell* path, const UCell* src )
{
    UBlockIt pi;
    const UBuffer* pbuf;
    UBuffer* buf;
    UCell* res;
    UCell* last;
    int type;
    int index;


    // The last path node is not included in the resolve function as it is
    // handled differently for assignment.
#if 1
    pbuf = ur_bufferSer( path );
    pi.it  = pbuf->ptr.cell;
    pi.end = pi.it + pbuf->used - 1;
#else
    ur_blockIt( ut, &pi, path );
    --pi.end;
#endif

    res = ur_push( ut, UT_UNSET );
    type = ur_pathResolve( ut, &pi, res, &last );
    ur_pop( ut );
    if( type == UR_THROW )
        return UR_THROW;

    type = ur_type(last);

    switch( ur_type(pi.it) )
    {
        case UT_INT:
            index = (int) ur_int(pi.it) - 1;
            if( ur_isSeriesType(type) )
            {
                if( ! (buf = ur_bufferSerM(last)) )
                    return UR_THROW;
                index += last->series.it;
                ((USeriesType*) ut->types[ type ])->poke( buf, index, src );
                return UR_OK;
            }
            else if( type == UT_VEC3 )
                return vec3_poke( ut, last, index, src );
            else if( type == UT_COORD )
                return coord_poke( ut, last, index, src );
            break;

        case UT_WORD:
            if( type == UT_CONTEXT )
            {
                if( ! (buf = ur_bufferSerM(last)) )
                    return UR_THROW;
                index = ur_ctxLookup( buf, ur_atom(pi.it) );
                if( index < 0 )
                    goto err;
                buf->ptr.cell[ index ] = *src;
                return UR_OK;
            }
            else if( type == UT_BLOCK )
            {
                if( ! (buf = ur_bufferSerM(last)) )
                    return UR_THROW;
                res = ur_blkSelectWord( buf, ur_atom(pi.it) );
                if( res )
                {
                    *res = *src;
                    return UR_OK;
                }
            }
            break;
    }

err:
    return ur_error( ut, UR_ERR_SCRIPT, "Cannot set path! (invalid node)" );
}


/** @} */ 


//EOF
