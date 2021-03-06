/*
 * Copyright 2010-2015 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "UnitDieBState.h"
#include "TileEngine.h"
#include "BattlescapeState.h"
#include "Map.h"
#include "../Engine/Game.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "../Mod/Mod.h"
#include "../Mod/Mod.h"
#include "../Engine/Sound.h"
#include "../Engine/RNG.h"
#include "../Engine/Options.h"
#include "../Engine/Language.h"
#include "../Mod/Armor.h"
#include "../Mod/Unit.h"
#include "InfoboxOKState.h"
#include "InfoboxState.h"
#include "../Savegame/Node.h"

namespace OpenXcom
{

/**
 * Sets up an UnitDieBState.
 * @param parent Pointer to the Battlescape.
 * @param unit Dying unit.
 * @param damageType Type of damage that caused the death.
 * @param noSound Whether to disable the death sound.
 */
UnitDieBState::UnitDieBState(BattlescapeGame *parent, BattleUnit *unit, ItemDamageType damageType, bool noSound) : BattleState(parent), _unit(unit), _damageType(damageType), _noSound(noSound), _extraFrame(0)
{
	// don't show the "fall to death" animation when a unit is blasted with explosives or he is already unconscious
	if (_damageType == DT_HE || _unit->getStatus() == STATUS_UNCONSCIOUS)
	{
		_unit->startFalling();

		while (_unit->getStatus() == STATUS_COLLAPSING)
		{
			_unit->keepFalling();
		}
	}
	else
	{
		if (_unit->getFaction() == FACTION_PLAYER)
		{
			_parent->getMap()->setUnitDying(true);
		}
		_parent->setStateInterval(BattlescapeState::DEFAULT_ANIM_SPEED);
		if (_unit->getDirection() != 3)
		{
			_parent->setStateInterval(BattlescapeState::DEFAULT_ANIM_SPEED / 3);
		}
	}

	_unit->clearVisibleTiles();
	_unit->clearVisibleUnits();

    if (_unit->getFaction() == FACTION_HOSTILE)
    {
        std::vector<Node *> *nodes = _parent->getSave()->getNodes();
        if (!nodes) return; // this better not happen.

        for (std::vector<Node*>::iterator  n = nodes->begin(); n != nodes->end(); ++n)
        {
            if (_parent->getSave()->getTileEngine()->distanceSq((*n)->getPosition(), _unit->getPosition()) < 4)
            {
                (*n)->setType((*n)->getType() | Node::TYPE_DANGEROUS);
            }
        }
    }
}

/**
 * Deletes the UnitDieBState.
 */
UnitDieBState::~UnitDieBState()
{

}

void UnitDieBState::init()
{
}

/**
 * Runs state functionality every cycle.
 * Progresses the death, displays any messages, checks if the mission is over, ...
 */
void UnitDieBState::think()
{
	if (_unit->getDirection() != 3 && _damageType != DT_HE)
	{
		int dir = _unit->getDirection() + 1;
		if (dir == 8)
		{
			dir = 0;
		}
		_unit->lookAt(dir);
		_unit->turn();
		if (dir == 3)
		{
			_parent->setStateInterval(BattlescapeState::DEFAULT_ANIM_SPEED);
		}
	}
	else if (_unit->getStatus() == STATUS_COLLAPSING)
	{
		_unit->keepFalling();
	}
	else if (!_unit->isOut())
	{
		_unit->startFalling();

		if (!_noSound)
		{
			playDeathSound();
		}
		if (_unit->getRespawn())
		{
			while (_unit->getStatus() == STATUS_COLLAPSING)
			{
				_unit->keepFalling();
			}
		}
	}
	if (_extraFrame == 2)
	{
		_parent->getMap()->setUnitDying(false);
		_parent->getTileEngine()->calculateUnitLighting();
		_parent->popState();
		if (_unit->getOriginalFaction() == FACTION_PLAYER)
		{
			Game *game = _parent->getSave()->getBattleState()->getGame();
			if (_unit->getStatus() == STATUS_DEAD)
			{
				if (_unit->getArmor()->getSize() == 1)
				{
					if (_damageType == DT_NONE && _unit->getSpawnUnit().empty())
					{
						game->pushState(new InfoboxOKState(game->getLanguage()->getString("STR_HAS_DIED_FROM_A_FATAL_WOUND", _unit->getGender()).arg(_unit->getName(game->getLanguage()))));
					}
					else if (Options::battleNotifyDeath)
					{
						game->pushState(new InfoboxState(game->getLanguage()->getString("STR_HAS_BEEN_KILLED", _unit->getGender()).arg(_unit->getName(game->getLanguage()))));
					}
				}
			}
			else
			{
				game->pushState(new InfoboxOKState(game->getLanguage()->getString("STR_HAS_BECOME_UNCONSCIOUS", _unit->getGender()).arg(_unit->getName(game->getLanguage()))));
			}
		}
		// if all units from either faction are killed - auto-end the mission.
		if (_parent->getSave()->getSide() == FACTION_PLAYER && Options::battleAutoEnd)
		{
			int liveAliens = 0;
			int liveSoldiers = 0;
			_parent->tallyUnits(liveAliens, liveSoldiers);

			if (liveAliens == 0 || liveSoldiers == 0)
			{
				_parent->getSave()->setSelectedUnit(0);
				_parent->cancelCurrentAction(true);
				_parent->requestEndTurn();
			}
		}
	}
	else if (_extraFrame == 1)
	{
		_extraFrame++;
	}
	else if (_unit->isOut())
	{
		_extraFrame = 1;
		if (!_noSound && _damageType == DT_HE && _unit->getStatus() != STATUS_UNCONSCIOUS)
		{
			playDeathSound();
		}
		if (_unit->getStatus() == STATUS_UNCONSCIOUS && _unit->getSpecialAbility() == SPECAB_EXPLODEONDEATH)
		{
			_unit->instaKill();
		}
		if (_unit->getTurnsSinceSpotted() < 255)
		{
			_unit->setTurnsSinceSpotted(255);
		}
		if (!_unit->getSpawnUnit().empty())
		{
			// converts the dead zombie to a chryssalid
			_parent->convertUnit(_unit);
		}
		else
		{
			convertUnitToCorpse();
		}
	}
	
	_parent->getMap()->cacheUnit(_unit);
}

/**
 * Unit falling cannot be cancelled.
 */
void UnitDieBState::cancel()
{
}

/**
 * Converts unit to a corpse (item).
 */
void UnitDieBState::convertUnitToCorpse()
{
	Position lastPosition = _unit->getPosition();
	int size = _unit->getArmor()->getSize();
	bool dropItems = (size == 1 && 
		(!Options::weaponSelfDestruction ||
		(_unit->getOriginalFaction() != FACTION_HOSTILE || _unit->getStatus() == STATUS_UNCONSCIOUS)));

	_parent->getSave()->getBattleState()->showPsiButton(false);
	// remove the unconscious body item corresponding to this unit, and if it was being carried, keep track of what slot it was in
	if (lastPosition != Position(-1,-1,-1))
	{
		_parent->getSave()->removeUnconsciousBodyItem(_unit);
	}

	// move inventory from unit to the ground
	if (dropItems)
	{
		std::vector<BattleItem*> itemsToKeep;
		for (std::vector<BattleItem*>::iterator i = _unit->getInventory()->begin(); i != _unit->getInventory()->end(); ++i)
		{
			_parent->dropItem(lastPosition, (*i));
			if (!(*i)->getRules()->isFixed())
			{
				(*i)->setOwner(0);
			}
			else
			{
				itemsToKeep.push_back(*i);
			}
		}

		_unit->getInventory()->clear();

		for (std::vector<BattleItem*>::iterator i = itemsToKeep.begin(); i != itemsToKeep.end(); ++i)
		{
			_unit->getInventory()->push_back(*i);
		}
	}

	// remove unit-tile link
	_unit->setTile(0);

	if (lastPosition == Position(-1,-1,-1)) // we're being carried
	{
		// replace the unconscious body item with a corpse in the carrying unit's inventory
		for (std::vector<BattleItem*>::iterator it = _parent->getSave()->getItems()->begin(); it != _parent->getSave()->getItems()->end(); )
		{
			if ((*it)->getUnit() == _unit)
			{
				RuleItem *corpseRules = _parent->getMod()->getItem(_unit->getArmor()->getCorpseBattlescape()[0]); // we're in an inventory, so we must be a 1x1 unit
				(*it)->convertToCorpse(corpseRules);
				break;
			}
			++it;
		}
	}
	else
	{
		int i = 0;
		for (int y = 0; y < size; y++)
		{
			for (int x = 0; x < size; x++)
			{
				BattleItem *corpse = new BattleItem(_parent->getMod()->getItem(_unit->getArmor()->getCorpseBattlescape()[i]), _parent->getSave()->getCurrentItemId());
				corpse->setUnit(_unit);
				if (_parent->getSave()->getTile(lastPosition + Position(x,y,0))->getUnit() == _unit) // check in case unit was displaced by another unit
				{
					_parent->getSave()->getTile(lastPosition + Position(x,y,0))->setUnit(0);
				}
				_parent->dropItem(lastPosition + Position(x,y,0), corpse, true);
				i++;
			}
		}
	}
}

/**
 * Plays the death sound.
 */
void UnitDieBState::playDeathSound()
{
	if (_unit->getDeathSound() == -1)
	{
		if (_unit->getGender() == GENDER_MALE)
		{
			_parent->getMod()->getSoundByDepth(_parent->getDepth(), Mod::MALE_SCREAM[RNG::generate(0, 2)])->play(-1, _parent->getMap()->getSoundAngle(_unit->getPosition()));
		}
		else
		{
			_parent->getMod()->getSoundByDepth(_parent->getDepth(), Mod::FEMALE_SCREAM[RNG::generate(0, 2)])->play(-1, _parent->getMap()->getSoundAngle(_unit->getPosition()));
		}
	}
	else if (_unit->getDeathSound() >= 0)
	{
		_parent->getMod()->getSoundByDepth(_parent->getDepth(), _unit->getDeathSound())->play(-1, _parent->getMap()->getSoundAngle(_unit->getPosition()));
	}
}

}
