#include "cmd/unit_generic.h"
#include "cmd/unit_util.h"
#include "universe_util.h"
#include "config_xml.h"

float max_allowable_travel_time()
{
    static float mat = XMLSupport::parse_float( vs_config->getVariable( "AI", "max_allowable_travel_time", "15" ) );
    return mat;
}

bool DistanceWarrantsWarpTo( Unit *parent, float dist, bool following )
{
    //first let us decide whether the target is far enough to warrant using warp
    //double dist =UnitUtil::getSignificantDistance(parent,target);
    static float tooclose = XMLSupport::parse_float( vs_config->getVariable( "AI", "too_close_for_warp_tactic", "13000" ) );
    static float tooclosefollowing =
        XMLSupport::parse_float( vs_config->getVariable( "AI", "too_close_for_warp_in_formation", "1500" ) );
    float toodamnclose    = following ? tooclosefollowing : tooclose;
    float diff = 1;
    parent->GetVelocityDifficultyMult( diff );
    float timetolive = dist/(diff*parent->GetComputerData().max_combat_speed);
    if ( timetolive > ( 5*max_allowable_travel_time() ) ) {
        return true;
    } else if ( timetolive > ( max_allowable_travel_time() ) ) {
        float mytime = SIMULATION_ATOM*1.5;
        static bool rampdown = XMLSupport::parse_bool( vs_config->getVariable( "physics", "autopilot_ramp_warp_down", "true" ) );
        if (rampdown == false) {
            static float warprampdowntime =
                XMLSupport::parse_float( vs_config->getVariable( "physics", "warprampdowntime", "0.5" ) );
            mytime = warprampdowntime;
        }
        if (dist-parent->GetWarpVelocity().Magnitude()*mytime < toodamnclose)
            return false;              //avoid nasty jitter-jumping behavior should eventually have "running away check"

        return true;
    }
    return false;
}

bool DistanceWarrantsTravelTo( Unit *parent, float dist, bool following )
{
    //first let us decide whether the target is far enough to warrant using warp
    float diff = 1;
    parent->GetVelocityDifficultyMult( diff );
    float timetolive = dist/(diff*parent->GetComputerData().max_combat_speed);
    if ( timetolive > max_allowable_travel_time() )
        return true;
    return false;
}

bool TargetWorthPursuing( Unit *parent, Unit *target )
{
    return true;
}

static void ActuallyWarpTo( Unit *parent, const QVector &tarpos, Vector tarvel, Unit *MatchSpeed = NULL )
{
    Vector vel = parent->GetVelocity();
    static float mindirveldot = XMLSupport::parse_float( vs_config->getVariable( "AI", "warp_cone", ".8" ) );
    static float mintarveldot = XMLSupport::parse_float( vs_config->getVariable( "AI", "match_velocity_cone", "-.8" ) );
    tarvel.Normalize();
    vel.Normalize();
    Vector dir = tarpos-parent->Position();
    dir.Normalize();
    float  dirveldot = dir.Dot( parent->cumulative_transformation_matrix.getR() );
    dir *= -1;
    float  chasedot  = dir.Dot( tarvel );
    if (dirveldot > mindirveldot) {
        static float min_energy_to_enter_warp =
            XMLSupport::parse_float( vs_config->getVariable( "AI", "min_energy_to_enter_warp", ".33" ) );
        if (parent->WarpEnergyData() > min_energy_to_enter_warp) {
            if (parent->graphicOptions.InWarp == 0) {
                parent->graphicOptions.InWarp = 1;                 //don't want the AI thrashing
                parent->graphicOptions.WarpRamping = 1;                 //don't want the AI thrashing
            }
        }
    } else {
        parent->graphicOptions.InWarp = 0;
    }
    static bool domatch = XMLSupport::parse_bool( vs_config->getVariable( "AI", "match_velocity_of_pursuant", "false" ) );
    if (chasedot > mintarveldot || !domatch)
        parent->computer.velocity_ref.SetUnit( NULL );
    else
        parent->computer.velocity_ref.SetUnit( MatchSpeed );
}

void WarpToP( Unit *parent, Unit *target, bool following )
{
    float dist = UnitUtil::getSignificantDistance( parent, target );
    if ( DistanceWarrantsWarpTo( parent, dist, following ) ) {
        if ( TargetWorthPursuing( parent, target ) ) {
            static bool auto_valid =
                XMLSupport::parse_bool( vs_config->getVariable( "physics", "insystem_jump_or_timeless_auto-pilot", "false" ) );
            if (auto_valid) {
                std::string tmp;
                parent->AutoPilotTo( target, false );
            } else {
                ActuallyWarpTo( parent, target->Position(), target->GetVelocity(), target );
            }
        }
    } else {
        parent->graphicOptions.InWarp = 0;
    }
}

void WarpToP( Unit *parent, const QVector &target, float radius, bool following )
{
    float dist = (parent->Position()-target).Magnitude()-radius-parent->rSize();
    if ( DistanceWarrantsWarpTo( parent, dist, following ) ) {
        static bool auto_valid =
            XMLSupport::parse_bool( vs_config->getVariable( "physics", "insystem_jump_or_timeless_auto-pilot", "false" ) );
        if (!auto_valid)
            ActuallyWarpTo( parent, target, QVector( 0, 0, .00001 ) );
    } else {
        parent->graphicOptions.InWarp = 0;
    }
}

