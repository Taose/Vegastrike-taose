using std::string;
//WARNING: Macro City ahead.  Please skip this section if you don't like macros.
static const char *error = "\nERROR: NULL Unit used in Python script; returning default value...";
#define CHECKME \
    }           \
    Unit *me = GetUnit(); if (!me) {fprintf( stderr, "%s", error ); return
#define WRAPPED0( type, name, def ) \
    type name() {                   \
        {CHECKME def; }             \
        return me->name(); }
#define WRAPPED1( type, name, atype, a, def ) \
    type name( atype a ) {                    \
        {CHECKME def; }                       \
        return me->name( a ); }
#define WRAPPED2( type, name, atype, a, btype, b, def ) \
    type name( atype a, btype b ) {                     \
        {CHECKME def; }                                 \
        return me->name( a, b ); }
#define WRAPPED3( type, name, atype, a, btype, b, ctype, c, def ) \
    type name( atype a, btype b, ctype c ) {                      \
        {CHECKME def; }                                           \
        return me->name( a, b, c ); }
#define WRAPPED4( type, name, atype, a, btype, b, ctype, c, dtype, d, def ) \
    type name( atype a, btype b, ctype c, dtype d ) {                       \
        {CHECKME def; }                                                     \
        return me->name( a, b, c, d ); }
#define voidWRAPPED0( name ) \
    void name() {            \
        {CHECKME; }          \
        me->name(); }
#define voidWRAPPED1( name, atype, a ) \
    void name( atype a ) {             \
        {CHECKME; }                    \
        me->name( a ); }
#define voidWRAPPED2( name, atype, a, btype, b ) \
    void name( atype a, btype b ) {              \
        {CHECKME; }                              \
        me->name( a, b ); }
#define voidWRAPPED3( name, atype, a, btype, b, ctype, c ) \
    void name( atype a, btype b, ctype c ) {               \
        {CHECKME; }                                        \
        me->name( a, b, c ); }
#define voidWRAPPED5( name, atype, a, btype, b, ctype, c, dtype, d, etype, e ) \
    void name( atype a,                                                        \
               btype b,                                                        \
               ctype c,                                                        \
               dtype d,                                                        \
               etype e ) {                                                     \
        {CHECKME; }                                                            \
        me->name( a, b, c, d, e ); }

#define EXPORT_UTIL0( type, name ) type name() {return UnitUtil::name( GetUnit() ); }
#define EXPORT_UTIL1( type, name, atype, a ) type name( atype a ) {return UnitUtil::name( GetUnit(), a ); }
#define EXPORT_UTIL2( type, name, atype, a, btype, b ) type name( atype a, btype b ) {return UnitUtil::name( GetUnit(), a, b ); }
#define EXPORT_UTIL3( type, name, atype, a, btype, b, ctype, c )   \
    type name( atype a, btype b, ctype c ) {return UnitUtil::name( \
                                                       GetUnit(),  \
                                                       a,          \
                                                       b,          \
                                                       c ); }
#define EXPORT_UTIL5( type, name, atype, a, btype, b, ctype, c, dtype, d, etype, e ) \
    type name( atype a,                                                              \
               btype b,                                                              \
               ctype c,                                                              \
               dtype d,                                                              \
               etype e ) {return UnitUtil::                                          \
                                 name( GetUnit(                                      \
                                              ), a, b, c, d, e ); }
#define voidEXPORT_UTIL0( name ) void name() {UnitUtil::name( GetUnit() ); }
#define voidEXPORT_UTIL1( name, atype, a ) void name( atype a ) {UnitUtil::name( GetUnit(), a ); }
#define voidEXPORT_UTIL2( name, atype, a, btype, b ) void name( atype a, btype b ) {UnitUtil::name( GetUnit(), a, b ); }
#define voidEXPORT_UTIL3( name, atype, a, btype, b, ctype, c ) \
    void name( atype a, btype b, ctype c ) {UnitUtil::name(    \
                                                GetUnit(), a, b, c ); }
#define voidEXPORT_UTIL5( name, atype, a, btype, b, ctype, c, dtype, d, etype, e ) \
    void name( atype a,                                                            \
               btype b,                                                            \
               ctype c,                                                            \
               dtype d,                                                            \
               etype e ) {UnitUtil::name(                                          \
                              GetUnit(),                                           \
                              a,                                                   \
                              b,                                                   \
                              c,                                                   \
                              d,                                                   \
                              e ); }
//#define EXPORT_UTIL(name,aff)
//#define voidEXPORT_UTIL(name) EXPORT_UTIL(name,0)
//End of Macro City
class UnitWrapper : public UnitContainer
{
public:
#include "python_unit_wrap.h"
///////////////////////////////MANUAL WRAP//////
//WRAPPED0(UnitCollection::UnitIterator, getSubUnits,0)
    UniverseUtil::PythonUnitIter getSubUnits()
    {
        {
            CHECKME UniverseUtil::PythonUnitIter();
        }
        return unit->getSubUnits();
    }
    void Kill()
    {
        {
            CHECKME;
        }
        unit->Kill( true );
    }
    UnitWrapper GetTarget()
    {
        {
            CHECKME 0;
        }
        return unit->Target();
    }
    UnitWrapper GetVelocityReference()
    {
        {
            CHECKME 0;
        }
        return unit->VelocityReference();
    }
    void SetVelocityReference( UnitWrapper targ )
    {
        {
            CHECKME;
        }
        unit->VelocityReference( targ );
    }
    void SetTarget( UnitWrapper targ )
    {
        {
            CHECKME;
        }
        unit->Target( targ );
    }
    boost::python::tuple GetOrientation()
    {
        {
            CHECKME VS_BOOST_MAKE_TUPLE( VS_BOOST_MAKE_TUPLE( 0, 0, 0 ), VS_BOOST_MAKE_TUPLE( 0, 0, 0 ), VS_BOOST_MAKE_TUPLE( 0,
                                                                                                                              0,
                                                                                                                              0 ) );
        }
        Vector p, q, r;
        unit->GetOrientation( p, q, r );
        return VS_BOOST_MAKE_TUPLE( VS_BOOST_MAKE_TUPLE( p.i, p.j, p.k ), VS_BOOST_MAKE_TUPLE( q.i,
                                                                                               q.j,
                                                                                               q.k ),
                                   VS_BOOST_MAKE_TUPLE( r.i, r.j, r.k ) );
    }
    boost::python::tuple rayCollide( QVector st, QVector en)
    {
        {
            CHECKME VS_BOOST_MAKE_TUPLE( 0, VS_BOOST_MAKE_TUPLE( 0, 0, 1 ), 0 );
        }
        float dist;
        UnitWrapper un;
        Vector nml;
        un = unit->rayCollide( st, en, nml, dist);
        boost::python::tuple ret = VS_BOOST_MAKE_TUPLE( un, nml, dist );
        return ret;
    }
    boost::python::tuple cosAngleToITTS( UnitWrapper target, float speed, float range )
    {
        {
            CHECKME VS_BOOST_MAKE_TUPLE_2( 0, 0 );
        }
        float dist;
        float ret = unit->cosAngleTo( target, dist, speed, range );
        return VS_BOOST_MAKE_TUPLE_2( ret, dist );
    }
    boost::python::tuple cosAngleTo( UnitWrapper target )
    {
        {
            CHECKME VS_BOOST_MAKE_TUPLE_2( 0, 0 );
        }
        float dist;
        float ret = unit->cosAngleTo( target, dist );
        return VS_BOOST_MAKE_TUPLE_2( ret, dist );
    }
    boost::python::tuple cosAngleFromMountTo( UnitWrapper target )
    {
        {
            CHECKME VS_BOOST_MAKE_TUPLE_2( 0, 0 );
        }
        float dist;
        float ret = unit->cosAngleFromMountTo( target, dist );
        return VS_BOOST_MAKE_TUPLE_2( ret, dist );
    }
    boost::python::tuple getAverageGunSpeed()
    {
        {
            CHECKME VS_BOOST_MAKE_TUPLE( 1, 0, 0 );
        }
        float speed, range, missilespeed;
        unit->getAverageGunSpeed( speed, range, missilespeed );
        return VS_BOOST_MAKE_TUPLE( speed, range, missilespeed );
    }
    boost::python::tuple InsideCollideTree( UnitWrapper smaller )
    {
        {
            CHECKME VS_BOOST_MAKE_TUPLE_4( VS_BOOST_MAKE_TUPLE( 0, 0, 0 ), VS_BOOST_MAKE_TUPLE( 0, 0, 0 ), VS_BOOST_MAKE_TUPLE(
                                              0,
                                              0,
                                              0 ), VS_BOOST_MAKE_TUPLE( 0, 0, 0 ) );
        }
        QVector bigpos, smallpos;
        Vector bigNormal, smallNormal;
        if ( !unit->InsideCollideTree( smaller, bigpos, bigNormal, smallpos, smallNormal ) ) {
            bigpos = smallpos = QVector( 0,
                                         0,
                                         0 );
        }
        boost::python::tuple tup = VS_BOOST_MAKE_TUPLE_4( VS_BOOST_MAKE_TUPLE( bigpos.i,
                                                                               bigpos.j,
                                                                               bigpos.k ),
                                                         VS_BOOST_MAKE_TUPLE( bigNormal.i,
                                                                              bigNormal.j,
                                                                              bigNormal.k ), VS_BOOST_MAKE_TUPLE( smallpos.i,
                                                                                                                  smallpos
                                                                                                                  .j,
                                                                                                                  smallpos
                                                                                                                  .k ),
                                                         VS_BOOST_MAKE_TUPLE( smallNormal.i, smallNormal.j, smallNormal.k ) );
        return tup;
    }
    BoostPythonDictionary GetMountInfo( int index ) const;
//UnitWrapper getSubUnit(int which) {{CHECKME 0;}un_iter it=unit->getSubUnits(); for (int i=0;i<which;i++) {it.advance();}return it.current();}
//UnitWrapper getFlightgroupLeader () {{CHECKME 0;}Flightgroup *group=unit->getFlightgroup();if (group) return group->leader; else return 0;}
//void setFlightgroupLeader (Unit * un) {{CHECKME;}Flightgroup *group=unit->getFlightgroup();if (group) group->leader.SetUnit(un);}
    float GetVelocityDifficultyMult()
    {
        {
            CHECKME 0;
        }
        float diff = 1;
        unit->GetVelocityDifficultyMult( diff );
        return diff;
    }
    int GetJumpStatus()
    {
        {
            CHECKME-1;
        }
        return unit->GetJumpStatus().drive;
    }
    void ApplyDamage( Vector pnt,
                      Vector normal,
                      float amt,
                      UnitWrapper dealer,
                      float phasedamage,
                      float r,
                      float g,
                      float b,
                      float a )
    {
        {
            CHECKME;
        }
        unit->ApplyDamage( pnt, normal, amt, unit, GFXColor( r, g, b, a ), dealer.GetUnit(), phasedamage );
    }
/////////////////////////////MANUAL WRAP//////
/*
 *  WRAPPED1(bool,TransferUnitToSystem,class StarSystem *,NewSystem,false)
 *  bool InCorrectStarSystem (StarSystem *active)
 *  //  PYTHON_DEFINE_METHOD(Class,&TransferUnitToSystem(unsigned int whichJumpQueue, class StarSystem * previouslyActiveStarSystem, bool DoSightAndSound) {{CHECKME 0;}class StarSystem * othActiveStarSystem=previouslyActiveStarSystem; unit->TransferUnitToSystem(whichJumpQueue,othActiveStarSystem,DoSightAndSound); return othActiveStarSystem;}
 *  //  class StarSystem * TransferUnitToSystem(unsigned int whichJumpQueue, class StarSystem * previouslyActiveStarSystem, bool DoSightAndSound) {{CHECKME 0;}class StarSystem * othActiveStarSystem=previouslyActiveStarSystem; unit->TransferUnitToSystem(whichJumpQueue,othActiveStarSystem,DoSightAndSound); return othActiveStarSystem;}
 */
/////////////////////////////////NO WRAP//////
    UnitWrapper( UnitContainer cont ) : UnitContainer( cont ) {}
    UnitWrapper( Unit *un = 0 ) : UnitContainer( un ) {}
    operator Unit *()
    {
        return GetUnit();
    }
    bool isNull()
    {
        Unit *un = GetUnit();
        if (un) if (un->GetHull() <= 0) return true;
        return un == 0;
    }
    bool notNull()
    {
        return !isNull();
    }
    void setNull()
    {
        SetUnit( 0 );
    }
    bool equal( UnitWrapper oth )
    {
        return this->operator==( oth );
    }
    bool notequal( UnitWrapper oth )
    {
        return this->operator!=( oth );
    }
};
#undef CHECKME
#undef WRAPPED0
#undef WRAPPED1
#undef WRAPPED2
#undef WRAPPED3
#undef WRAPPED4
#undef voidWRAPPED0
#undef voidWRAPPED1
#undef voidWRAPPED2
#undef voidWRAPPED3
#undef voidWRAPPED5
#undef EXPORT_UTIL0
#undef EXPORT_UTIL1
#undef EXPORT_UTIL2
#undef EXPORT_UTIL3
#undef EXPORT_UTIL5
#undef voidEXPORT_UTIL0
#undef voidEXPORT_UTIL1
#undef voidEXPORT_UTIL2
#undef voidEXPORT_UTIL3
#undef voidEXPORT_UTIL5

