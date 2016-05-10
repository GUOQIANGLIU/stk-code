//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2016 SuperTuxKart-Team
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include "karts/controller/soccer_ai.hpp"

#include "items/attachment.hpp"
#include "items/powerup.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/controller/kart_control.hpp"
#include "karts/kart_properties.hpp"
#include "modes/soccer_world.hpp"
#include "tracks/battle_graph.hpp"

#ifdef AI_DEBUG
#include "irrlicht.h"
#include <iostream>
using namespace irr;
using namespace std;
#endif

SoccerAI::SoccerAI(AbstractKart *kart)
        : ArenaAI(kart)
{

    reset();

#ifdef AI_DEBUG
    video::SColor col_debug(128, 128, 0, 0);
    video::SColor col_debug_next(128, 0, 128, 128);
    m_debug_sphere = irr_driver->addSphere(1.0f, col_debug);
    m_debug_sphere->setVisible(true);
    m_debug_sphere_next = irr_driver->addSphere(1.0f, col_debug_next);
    m_debug_sphere_next->setVisible(true);
#endif
    m_world = dynamic_cast<SoccerWorld*>(World::getWorld());
    m_track = m_world->getTrack();
    m_cur_team = m_world->getKartTeam(m_kart->getWorldKartId());

    // Don't call our own setControllerName, since this will add a
    // billboard showing 'AIBaseController' to the kart.
    Controller::setControllerName("SoccerAI");

}   // SoccerAI

//-----------------------------------------------------------------------------

SoccerAI::~SoccerAI()
{
#ifdef AI_DEBUG
    irr_driver->removeNode(m_debug_sphere);
    irr_driver->removeNode(m_debug_sphere_next);
#endif
}   //  ~SoccerAI

//-----------------------------------------------------------------------------
/** Resets the AI when a race is restarted.
 */
void SoccerAI::reset()
{
    ArenaAI::reset();
    AIBaseController::reset();

    m_saving_ball = false;
    m_force_brake = false;
}   // reset

//-----------------------------------------------------------------------------
void SoccerAI::update(float dt)
{
    m_saving_ball = false;
    m_force_brake = false;

    if (World::getWorld()->getPhase() == World::GOAL_PHASE)
    {
        m_controls->m_brake = false;
        m_controls->m_accel = 0.0f;
        AIBaseController::update(dt);
        return;
    }

    ArenaAI::update(dt);
}   // update

//-----------------------------------------------------------------------------
void SoccerAI::findClosestKart(bool use_difficulty)
{
    float distance = 99999.9f;
    const unsigned int n = m_world->getNumKarts();
    int closest_kart_num = 0;

    for (unsigned int i = 0; i < n; i++)
    {
        const AbstractKart* kart = m_world->getKart(i);
        if (kart->isEliminated()) continue;

        if (kart->getWorldKartId() == m_kart->getWorldKartId())
            continue; // Skip the same kart

        if (m_world->getKartTeam(kart
            ->getWorldKartId()) == m_world->getKartTeam(m_kart
            ->getWorldKartId()))
            continue; // Skip the kart with the same team

        Vec3 d = kart->getXYZ() - m_kart->getXYZ();
        if (d.length_2d() <= distance)
        {
            distance = d.length_2d();
            closest_kart_num = i;
        }
    }

    const AbstractKart* closest_kart = m_world->getKart(closest_kart_num);
    m_closest_kart_node = m_world->getKartNode(closest_kart_num);
    m_closest_kart_point = closest_kart->getXYZ();
    m_closest_kart = m_world->getKart(closest_kart_num);
    checkPosition(m_closest_kart_point, &m_closest_kart_pos_data);

}   // findClosestKart

//-----------------------------------------------------------------------------
void SoccerAI::findTarget()
{
    // Check whether any defense is needed
    if ((m_world->getBallPosition() - m_world->getGoalLocation(m_cur_team,
        CheckGoal::POINT_CENTER)).length_2d() < 50.0f &&
        m_world->getDefender(m_cur_team) == (signed)m_kart->getWorldKartId())
    {
        m_target_node = m_world->getBallNode();
        m_target_point = correctBallPosition(m_world->getBallPosition());
        return;
    }

    // Find a suitable target to drive to, either ball or powerup
    if ((m_world->getBallPosition() - m_kart->getXYZ()).length_2d() > 20.0f &&
        (m_kart->getPowerup()->getType() == PowerupManager::POWERUP_NOTHING &&
         m_kart->getAttachment()->getType() != Attachment::ATTACH_SWATTER))
        collectItemInArena(&m_target_point , &m_target_node);
    else
    {
        m_target_node = m_world->getBallNode();
        m_target_point = correctBallPosition(m_world->getBallPosition());
    }

}   // findTarget

//-----------------------------------------------------------------------------
Vec3 SoccerAI::correctBallPosition(const Vec3& orig_pos)
{
    // Notice: Build with AI_DEBUG and change camera target to an AI kart,
    // to debug or see how AI steer with the ball
    posData ball_pos = {0};
    Vec3 ball_lc;
    checkPosition(orig_pos, &ball_pos, &ball_lc);

    // Too far / behind from the ball,
    // use path finding from arena ai to get close
    if (!(ball_pos.distance < 3.0f) || ball_pos.behind) return orig_pos;

    // Save the opposite team
    SoccerTeam opp_team = (m_cur_team == SOCCER_TEAM_BLUE ?
        SOCCER_TEAM_RED : SOCCER_TEAM_BLUE);

    // Prevent lost control when steering with ball
    const bool need_braking = ball_pos.angle > 0.1f &&
        m_kart->getSpeed() > 9.0f && ball_pos.distance <2.5f;

    // Goal / own goal detection first, as different aiming method will be used
    const bool likely_to_goal =
        ball_pos.angle < 0.2f && isLikelyToGoal(opp_team);
    if (likely_to_goal)
    {
        if (need_braking)
        {
            m_controls->m_brake = true;
            m_force_brake = true;
            // Prevent keep pushing by aiming a nearer point
            return m_kart->getTrans()(ball_lc - Vec3(0, 0, 1));
        }
        // Keep pushing the ball straight to the goal
        return orig_pos;
    }

    const bool likely_to_own_goal =
        ball_pos.angle < 0.2f && isLikelyToGoal(m_cur_team);
    if (likely_to_own_goal)
    {
        // It's getting likely to own goal, apply more
        // offset for skidding, to save the ball from behind
        // scored.
        if (m_cur_difficulty == RaceManager::DIFFICULTY_HARD ||
            m_cur_difficulty == RaceManager::DIFFICULTY_BEST)
            m_saving_ball = true;
        return m_kart->getTrans()(ball_pos.lhs ?
            ball_lc - Vec3(2, 0, 0) + Vec3(0, 0, 2) :
            ball_lc + Vec3(2, 0, 2));
    }

    // Now try to make the ball face towards the goal
    // Aim at upper/lower left/right corner of ball depends on location
    posData goal_pos = {0};
    checkPosition(m_world->getGoalLocation(opp_team, CheckGoal::POINT_CENTER),
        &goal_pos);
    Vec3 corrected_pos;
    if (ball_pos.lhs && goal_pos.lhs)
    {
        corrected_pos = ball_lc + Vec3(1, 0, 1);
    }
    else if (!ball_pos.lhs && !goal_pos.lhs)
    {
        corrected_pos = ball_lc - Vec3(1, 0, 0) + Vec3(0, 0, 1);
    }
    else if (!ball_pos.lhs && goal_pos.lhs)
    {
        corrected_pos = ball_lc + Vec3(1, 0, 0) - Vec3(0, 0, 1);
    }
    else
    {
        corrected_pos = ball_lc - Vec3(1, 0, 1);
    }
    if (need_braking)
    {
        m_controls->m_brake = true;
        m_force_brake = true;
    }
    return m_kart->getTrans()(corrected_pos);

}   // correctBallPosition

//-----------------------------------------------------------------------------
bool SoccerAI::isLikelyToGoal(SoccerTeam team) const
{
    // Use local coordinate for easy compare
    Vec3 first_pos;
    Vec3 last_pos;
    checkPosition(m_world->getGoalLocation(team, CheckGoal::POINT_FIRST),
        NULL, &first_pos);
    checkPosition(m_world->getGoalLocation(team, CheckGoal::POINT_LAST),
        NULL, &last_pos);

    // If the kart lies between the first and last pos, and faces
    // in front of them, than it's likely to goal
    if ((first_pos.z() > 0.0f && last_pos.z() > 0.0f) &&
        ((first_pos.x() < 0.0f && last_pos.x() > 0.0f) ||
        (last_pos.x() < 0.0f && first_pos.x() > 0.0f)))
        return true;

    return false;
}   // isLikelyToGoal
//-----------------------------------------------------------------------------
int SoccerAI::getCurrentNode() const
{
    return m_world->getKartNode(m_kart->getWorldKartId());
}   // getCurrentNode
//-----------------------------------------------------------------------------
bool SoccerAI::isWaiting() const
{
    return m_world->isStartPhase();
}   // isWaiting
