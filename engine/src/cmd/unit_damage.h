#ifndef __UNIT_DAMAGE_CPP__
#define __UNIT_DAMAGE_CPP__
#include <string>
#include <vector>
#include "unit.h"
#include "unit_factory.h"
#include "ai/order.h"
#include "gfx/animation.h"
#include "gfx/mesh.h"
#include "gfx/halo.h"
#include "vegastrike.h"
#include "unit_collide.h"
#include <float.h>
#include "audiolib.h"
#include "images.h"
#include "beam.h"
#include "config_xml.h"
#include "vs_globals.h"
#include "xml_support.h"
#include "savegame.h"
#include "gfx/cockpit.h"
#include "cmd/script/mission.h"
#include "missile.h"
#include "cmd/ai/communication.h"
#include "cmd/script/flightgroup.h"
#include "music.h"
#include "faction_generic.h"
#include "universe_util.h"
#include "csv.h"
#include "unit_csv.h"
#include "base.h"

extern unsigned int apply_float_to_unsigned_int( float tmp );  //Short fix
extern vector< Mesh* >MakeMesh( unsigned int mysize );

template < class UnitType >
void GameUnit< UnitType >::Split( int level )
{
    static bool split_subunits = XMLSupport::parse_bool( vs_config->getVariable( "graphics", "split_dead_subunits", "true" ) );
    if (split_subunits)
        for (un_iter su = this->getSubUnits(); *su; ++su)
            (*su)->Split( level );
    static float debrismassmult = XMLSupport::parse_float( vs_config->getVariable( "physics", "debris_mass", ".00001" ) );
    Vector PlaneNorm;
    for (int i = 0; i < nummesh();) {
        if (this->meshdata[i]) {
            if (this->meshdata[i]->getBlendDst() == ONE) {
                delete this->meshdata[i];
                this->meshdata.erase( this->meshdata.begin()+i );
            } else {i++; }} else {this->meshdata.erase( this->meshdata.begin()+i ); }}
    int    nm = this->nummesh();
    string fac = FactionUtil::GetFaction( this->faction );

    CSVRow unit_stats( LookupUnitRow( this->name, fac ) );
    unsigned int num_chunks = unit_stats.success() ? atoi( unit_stats["Num_Chunks"].c_str() ) : 0;
    if (nm <= 0 && num_chunks == 0)
        return;
    vector< Mesh* >old = this->meshdata;
    Mesh *shield = old.back();
    old.pop_back();

    vector< unsigned int >meshsizes;
    if ( num_chunks && unit_stats.success() ) {
        size_t i;
        vector< Mesh* >nw;
        unsigned int which_chunk = rand()%num_chunks;
        string  chunkname   = UniverseUtil::LookupUnitStat( this->name, fac, "Chunk_"+XMLSupport::tostring( which_chunk ) );
        string  dir = UniverseUtil::LookupUnitStat( this->name, fac, "Directory" );
        VSFileSystem::current_path.push_back( unit_stats.getRoot() );
        VSFileSystem::current_subdirectory.push_back( "/"+dir );
        VSFileSystem::current_type.push_back( UnitFile );
        float randomstartframe   = 0;
        float randomstartseconds = 0;
        string scalestr     = UniverseUtil::LookupUnitStat( this->name, fac, "Unit_Scale" );
        int   scale = atoi( scalestr.c_str() );
        if (scale == 0) scale = 1;
        AddMeshes( nw, randomstartframe, randomstartseconds, scale, chunkname, this->faction,
                   this->getFlightgroup(), &meshsizes );
        VSFileSystem::current_type.pop_back();
        VSFileSystem::current_subdirectory.pop_back();
        VSFileSystem::current_path.pop_back();
        for (i = 0; i < old.size(); ++i)
            delete old[i];
        old.clear();
        old = nw;
    } else {
        for (int split = 0; split < level; split++) {
            vector< Mesh* >nw;
            size_t oldsize = old.size();
            for (size_t i = 0; i < oldsize; i++) {
                PlaneNorm.Set( rand()-RAND_MAX/2, rand()-RAND_MAX/2, rand()-RAND_MAX/2+.5 );
                PlaneNorm.Normalize();
                nw.push_back( NULL );
                nw.push_back( NULL );
                old[i]->Fork( nw[nw.size()-2], nw.back(), PlaneNorm.i, PlaneNorm.j, PlaneNorm.k,
                             -PlaneNorm.Dot( old[i]->Position() ) );                                                                              //splits somehow right down the middle.
                delete old[i];
                old[i] = NULL;
                if (nw[nw.size()-2] == NULL) {
                    nw[nw.size()-2] = nw.back();
                    nw.pop_back();
                }
                if (nw.back() == NULL)
                    nw.pop_back();
            }
            old = nw;
        }
        meshsizes.reserve( old.size() );
        for (size_t i = 0; i < old.size(); ++i)
            meshsizes.push_back( 1 );
    }
    old.push_back( NULL );     //push back shield
    if (shield)
        delete shield;
    nm = old.size()-1;
    unsigned int k = 0;
    vector< Mesh* >tempmeshes;
    for (vector<Mesh *>::size_type i=0;i<meshsizes.size();i++) {
        Unit *splitsub;
        tempmeshes.clear();
        tempmeshes.reserve( meshsizes[i] );
        for (unsigned int j = 0; j < meshsizes[i] && k < old.size(); ++j, ++k)
            tempmeshes.push_back( old[k] );
        this->SubUnits.prepend( splitsub = UnitFactory::createUnit( tempmeshes, true, this->faction ) );
        splitsub->hull = 1000;
        splitsub->name = "debris";
        splitsub->Mass = debrismassmult*splitsub->Mass/level;
        splitsub->pImage->timeexplode = .1;
        if (splitsub->meshdata[0]) {
            Vector loc  = splitsub->meshdata[0]->Position();
            static float explosion_force = XMLSupport::parse_float( vs_config->getVariable( "graphics", "explosionforce", ".5" ) );             //10 seconds for auto to kick in;
            float  locm = loc.Magnitude();
            if (locm < .0001)
                locm = 1;
            splitsub->ApplyForce( splitsub->meshdata[0]->rSize()*explosion_force*10*splitsub->GetMass()*loc/locm );
            loc.Set( rand(), rand(), rand()+.1 );
            loc.Normalize();
            static float explosion_torque =
                XMLSupport::parse_float( vs_config->getVariable( "graphics", "explosiontorque", ".001" ) );                                     //10 seconds for auto to kick in;
            splitsub->ApplyLocalTorque( loc*splitsub->GetMoment()*explosion_torque*( 1+rand()%(int) ( 1+this->rSize() ) ) );
        }
    }
    old.clear();
    this->meshdata.clear();
    this->meshdata.push_back( NULL );     //the shield
    this->Mass *= debrismassmult;
}

extern float rand01();

template < class UnitType >
void GameUnit< UnitType >::ArmorDamageSound( const Vector &pnt )
{
    if ( !_Universe->isPlayerStarship( this ) ) {
        static bool ai_sound = XMLSupport::parse_bool( vs_config->getVariable( "audio", "ai_sound", "true" ) );
        if ( AUDIsPlaying( this->sound->armor ) )
            AUDStopPlaying( this->sound->armor );
        if (ai_sound)
            AUDPlay( this->sound->armor, this->ToWorldCoordinates(
                         pnt ).Cast()+this->cumulative_transformation.position, this->Velocity, 1 );
    } else {
        static int playerarmorsound =
            AUDCreateSoundWAV( vs_config->getVariable( "unitaudio", "player_armor_hit", "bigarmor.wav" ) );
        int sound = playerarmorsound != -1 ? playerarmorsound : this->sound->armor;
        if ( AUDIsPlaying( sound ) )
            AUDStopPlaying( sound );
        AUDPlay( sound, this->ToWorldCoordinates(
            pnt ).Cast()+this->cumulative_transformation.position, this->Velocity, 1 );
    }
}

template < class UnitType >
void GameUnit< UnitType >::HullDamageSound( const Vector &pnt )
{
    if ( !_Universe->isPlayerStarship( this ) ) {
        static bool ai_sound = XMLSupport::parse_bool( vs_config->getVariable( "audio", "ai_sound", "true" ) );
        if ( AUDIsPlaying( this->sound->hull ) )
            AUDStopPlaying( this->sound->hull );
        if (ai_sound)
            AUDPlay( this->sound->hull, this->ToWorldCoordinates(
                         pnt ).Cast()+this->cumulative_transformation.position, this->Velocity, 1 );
    } else {
        static int playerhullsound = AUDCreateSoundWAV( vs_config->getVariable( "unitaudio", "player_hull_hit", "bigarmor.wav" ) );
        int sound = playerhullsound != -1 ? playerhullsound : this->sound->hull;
        if ( AUDIsPlaying( sound ) )
            AUDStopPlaying( sound );
        AUDPlay( sound, this->ToWorldCoordinates(
            pnt ).Cast()+this->cumulative_transformation.position, this->Velocity, 1 );
    }
}

template < class UnitType >
float GameUnit< UnitType >::DealDamageToShield( const Vector &pnt, float &damage )
{
    float percent = UnitType::DealDamageToShield( pnt, damage );
    if ( !_Universe->isPlayerStarship( this ) ) {
        static bool ai_sound = XMLSupport::parse_bool( vs_config->getVariable( "audio", "ai_sound", "true" ) );
        if (percent) {
            if ( AUDIsPlaying( this->sound->shield ) )
                AUDStopPlaying( this->sound->shield );
            if (ai_sound)
                AUDPlay( this->sound->shield, this->ToWorldCoordinates(
                    pnt ).Cast()+this->cumulative_transformation.position, this->Velocity, 1 );
        }
    } else {
        static int playerhullsound =
            AUDCreateSoundWAV( vs_config->getVariable( "unitaudio", "player_shield_hit", "shieldhit.wav" ) );
        int sound = playerhullsound != -1 ? playerhullsound : this->sound->hull;
        if (percent) {
            if ( AUDIsPlaying( sound ) )
                AUDStopPlaying( sound );
            AUDPlay( sound, this->ToWorldCoordinates(
                pnt ).Cast()+this->cumulative_transformation.position, this->Velocity, 1 );
        }
    }
    return percent;
}

extern Animation * GetVolatileAni( unsigned int );
extern unsigned int AddAnimation( const QVector&, const float, bool, const string&, float percentgrow );

extern Animation * getRandomCachedAni();
extern string getRandomCachedAniString();
extern void disableSubUnits( Unit *un );

template < class UnitType >
bool GameUnit< UnitType >::Explode( bool drawit, float timeit )
{
    if (this->pImage->pExplosion == NULL && this->pImage->timeexplode == 0) {
        //no explosion in unit data file && explosions haven't started yet

        //notify the director that a ship got destroyed
        mission->DirectorShipDestroyed( this );
        disableSubUnits( this );
        this->pImage->timeexplode = 0;
        static string expani = vs_config->getVariable( "graphics", "explosion_animation", "explosion_orange.ani" );

        string bleh = this->pImage->explosion_type;
        if ( bleh.empty() )
            FactionUtil::GetRandExplosionAnimation( this->faction, bleh );
        if ( bleh.empty() ) {
            static Animation cache( expani.c_str(), false, .1, BILINEAR, false );
            bleh = getRandomCachedAniString();
            if (bleh.size() == 0)
                bleh = expani;
        }
        static bool explosion_face_player =
            XMLSupport::parse_bool( vs_config->getVariable( "graphics", "explosion_face_player", "true" ) );
        this->pImage->pExplosion = new Animation( bleh.c_str(), explosion_face_player, .1, BILINEAR, true );
        this->pImage->pExplosion->SetDimensions( this->ExplosionRadius(), this->ExplosionRadius() );
        Vector p, q, r;
        this->GetOrientation( p, q, r );
        this->pImage->pExplosion->SetOrientation( p, q, r );
        if (this->isUnit() != MISSILEPTR) {
            static float expdamagecenter =
                XMLSupport::parse_float( vs_config->getVariable( "physics", "explosion_damage_center", "1" ) );
            static float damageedge =
                XMLSupport::parse_float( vs_config->getVariable( "graphics", "explosion_damage_edge", ".125" ) );
            _Universe->activeStarSystem()->AddMissileToQueue( new MissileEffect( this->Position().Cast(), this->MaxShieldVal(),
                                                                                 0, this->ExplosionRadius()*expdamagecenter,
                                                                                 this->ExplosionRadius()*expdamagecenter
                                                                                 *damageedge, NULL ) );
        }
        QVector exploc = this->cumulative_transformation.position;
        bool    sub    = this->isSubUnit();
        Unit   *un     = NULL;
        if (!sub)
            if (( un = _Universe->AccessCockpit( 0 )->GetParent() )) {
                static float explosion_closeness =
                    XMLSupport::parse_float( vs_config->getVariable( "audio", "explosion_closeness", ".8" ) );
                exploc = un->Position()*explosion_closeness+exploc*(1-explosion_closeness);
            }
        AUDPlay( this->sound->explode, exploc, this->Velocity, 1 );
        if (!sub) {
            un = _Universe->AccessCockpit()->GetParent();
            if (this->isUnit() == UNITPTR) {
                static float percentage_shock =
                    XMLSupport::parse_float( vs_config->getVariable( "graphics", "percent_shockwave", ".5" ) );
                if ( rand() < RAND_MAX*percentage_shock && ( !this->isSubUnit() ) ) {
                    static float      shockwavegrowth =
                        XMLSupport::parse_float( vs_config->getVariable( "graphics", "shockwave_growth", "1.05" ) );
                    static string     shockani( vs_config->getVariable( "graphics", "shockwave_animation", "explosion_wave.ani" ) );

                    static Animation *__shock__ani = new Animation( shockani.c_str(), true, .1, MIPMAP, false );
                    __shock__ani->SetFaceCam( false );
                    unsigned int      which = AddAnimation( this->Position(),
                                                            this->ExplosionRadius(), true, shockani, shockwavegrowth );
                    Animation *ani = GetVolatileAni( which );
                    if (ani) {
                        ani->SetFaceCam( false );
                        Vector p, q, r;
                        this->GetOrientation( p, q, r );
                        int    tmp = rand();
                        if (tmp < RAND_MAX/24)
                            ani->SetOrientation( Vector( 0, 0, 1 ), Vector( 1, 0, 0 ), Vector( 0, 1, 0 ) );
                        else if (tmp < RAND_MAX/16)
                            ani->SetOrientation( Vector( 0, 1, 0 ), Vector( 0, 0, 1 ), Vector( 1, 0, 0 ) );
                        else if (tmp < RAND_MAX/8)
                            ani->SetOrientation( Vector( 1, 0, 0 ), Vector( 0, 1, 0 ), Vector( 0, 0, 1 ) );
                        else
                            ani->SetOrientation( p, q, r );
                    }
                }
                if (un) {
                    int upgradesfaction    = FactionUtil::GetUpgradeFaction();
                    static float badrel    =
                        XMLSupport::parse_float( vs_config->getVariable( "sound", "loss_relationship", "-.1" ) );
                    static float goodrel   =
                        XMLSupport::parse_float( vs_config->getVariable( "sound", "victory_relationship", ".5" ) );
                    static float timelapse =
                        XMLSupport::parse_float( vs_config->getVariable( "sound", "time_between_music", "180" ) );
                    float rel = un->getRelation( this );
                    if (!BaseInterface::CurrentBase) {
                        static float lasttime = 0;
                        float newtime = getNewTime();
                        if ( newtime-lasttime > timelapse
                            || (_Universe->isPlayerStarship( this ) && this->isUnit() != MISSILEPTR && this->faction
                                != upgradesfaction) ) {
                            //No victory for missiles or spawned explosions
                            if (rel > goodrel) {
                                lasttime = newtime;
                                muzak->SkipRandSong( Music::LOSSLIST );
                            } else if (rel < badrel) {
                                lasttime = newtime;
                                muzak->SkipRandSong( Music::VICTORYLIST );
                            }
                        }
                    }
                }
            }
        }
    }
    static float timebeforeexplodedone = XMLSupport::parse_float( vs_config->getVariable( "physics", "debris_time", "500" ) );
    bool timealldone =
        ( this->pImage->timeexplode > timebeforeexplodedone || this->isUnit() == MISSILEPTR
         || _Universe->AccessCockpit()->GetParent() == this || this->SubUnits.empty() );
    if (this->pImage->pExplosion) {
        this->pImage->timeexplode += timeit;
        this->pImage->pExplosion->SetPosition( this->Position() );
        Vector p, q, r;
        this->GetOrientation( p, q, r );
        this->pImage->pExplosion->SetOrientation( p, q, r );
        if (this->pImage->pExplosion->Done() && timealldone) {
            delete this->pImage->pExplosion;
            this->pImage->pExplosion = NULL;
        }
        if (drawit && this->pImage->pExplosion)
            this->pImage->pExplosion->Draw();              //puts on draw queue... please don't delete
    }
    bool alldone = this->pImage->pExplosion ? !this->pImage->pExplosion->Done() : false;
    if ( !this->SubUnits.empty() ) {
        Unit *su;
        for (un_iter ui = this->getSubUnits(); (su = *ui); ++ui) {
            bool temp = su->Explode( drawit, timeit );
            if (su->GetImageInformation().pExplosion)
                alldone |= temp;
        }
    }
    static float phatloot = XMLSupport::parse_float( vs_config->getVariable( "physics", "eject_cargo_on_blowup", "0" ) );
    if ( (phatloot > 0) && (this->numCargo() > 0) ) {
        size_t dropcount = (size_t) floor( this->numCargo()/phatloot )+1;
        if ( dropcount > this->numCargo() ) dropcount = this->numCargo();
        for (size_t i = 0; i < dropcount; i++)
            this->EjectCargo( this->numCargo()-1 );              //Ejecting the last one is somewhat faster
    }
    return alldone || (!timealldone);
}

#endif

