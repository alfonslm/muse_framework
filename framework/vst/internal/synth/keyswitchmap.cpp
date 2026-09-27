/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "keyswitchmap.h"

#include <algorithm>
#include <cctype>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include "log.h"

using namespace muse::mpe;

namespace muse::vst {
namespace {
#define AT_NAME(x) { #x, ArticulationType::x }

//! NOTE Canonical map keys: the exact ArticulationType identifier name.
const std::unordered_map<std::string, ArticulationType> CANONICAL_NAMES {
    AT_NAME(Standard), AT_NAME(Staccato), AT_NAME(Staccatissimo), AT_NAME(Tenuto), AT_NAME(Marcato),
    AT_NAME(Accent), AT_NAME(SoftAccent), AT_NAME(LaissezVibrer), AT_NAME(Subito), AT_NAME(FadeIn), AT_NAME(FadeOut),
    AT_NAME(Harmonic), AT_NAME(JazzTone), AT_NAME(PalmMute), AT_NAME(Mute), AT_NAME(Open), AT_NAME(Pizzicato),
    AT_NAME(SnapPizzicato), AT_NAME(RandomPizzicato), AT_NAME(UpBow), AT_NAME(DownBow), AT_NAME(Detache),
    AT_NAME(Martele), AT_NAME(Jete), AT_NAME(ColLegno), AT_NAME(SulPont), AT_NAME(SulTasto),
    AT_NAME(GhostNote), AT_NAME(CrossNote), AT_NAME(CrossLargeNote), AT_NAME(CrossOrnateNote), AT_NAME(CircleNote),
    AT_NAME(CircleCrossNote), AT_NAME(CircleDotNote), AT_NAME(TriangleLeftNote), AT_NAME(TriangleRightNote),
    AT_NAME(TriangleUpNote), AT_NAME(TriangleDownNote), AT_NAME(TriangleRoundDownNote), AT_NAME(DiamondNote),
    AT_NAME(MoonNote), AT_NAME(PlusNote), AT_NAME(SlashNote), AT_NAME(SquareNote), AT_NAME(SlashedBackwardsNote),
    AT_NAME(SlashedForwardsNote),
    AT_NAME(Fall), AT_NAME(QuickFall), AT_NAME(Doit), AT_NAME(Plop), AT_NAME(Scoop), AT_NAME(BrassBend),
    AT_NAME(SlideOutDown), AT_NAME(SlideOutUp), AT_NAME(SlideInAbove), AT_NAME(SlideInBelow), AT_NAME(VolumeSwell),
    AT_NAME(Vibrato), AT_NAME(WideVibrato), AT_NAME(MoltoVibrato), AT_NAME(SenzaVibrato),
    AT_NAME(Tremolo8th), AT_NAME(Tremolo16th), AT_NAME(Tremolo32nd), AT_NAME(Tremolo64th), AT_NAME(TremoloBuzz),
    AT_NAME(TrillBaroque), AT_NAME(UpperMordent), AT_NAME(LowerMordent), AT_NAME(UpperMordentBaroque),
    AT_NAME(LowerMordentBaroque), AT_NAME(PrallMordent), AT_NAME(MordentWithUpperPrefix), AT_NAME(UpMordent),
    AT_NAME(DownMordent), AT_NAME(Tremblement), AT_NAME(UpPrall), AT_NAME(PrallUp), AT_NAME(PrallDown),
    AT_NAME(LinePrall), AT_NAME(Slide), AT_NAME(Turn), AT_NAME(InvertedTurn),
    AT_NAME(PreAppoggiatura), AT_NAME(PostAppoggiatura), AT_NAME(Acciaccatura),
    AT_NAME(TremoloBar), AT_NAME(Distortion), AT_NAME(Overdrive), AT_NAME(Slap), AT_NAME(Pop),
    AT_NAME(LeftHandTapping), AT_NAME(RightHandTapping),
    AT_NAME(ContinuousGlissando), AT_NAME(Breath),
    AT_NAME(MalletBellOnTable), AT_NAME(MalletBellSuspended), AT_NAME(MalletLift), AT_NAME(Pluck), AT_NAME(PluckLift),
    AT_NAME(Gyro), AT_NAME(Martellato), AT_NAME(MartellatoLift), AT_NAME(HandMartellato), AT_NAME(MutedMartellato),
    AT_NAME(ThumbDamp), AT_NAME(BrushDamp), AT_NAME(Ring), AT_NAME(RingTouch), AT_NAME(SingingBell),
    AT_NAME(SingingVibrate), AT_NAME(Swing), AT_NAME(Echo),
    AT_NAME(Trill), AT_NAME(Crescendo), AT_NAME(Diminuendo), AT_NAME(DiscreteGlissando), AT_NAME(Legato),
    AT_NAME(Pedal), AT_NAME(Multibend), AT_NAME(Arpeggio), AT_NAME(ArpeggioUp), AT_NAME(ArpeggioDown),
    AT_NAME(ArpeggioStraightUp), AT_NAME(ArpeggioStraightDown),
};

#undef AT_NAME

//! NOTE Lower-case names Keyswitch Creator sets use in articulationKeyMap/techniqueKeyMap,
//! accepted so Alfons's existing sets carry over without a full rewrite. Only the simple,
//! single-technique names are aliased -- compound/vendor-specific keyswitch labels
//! (e.g. "soft accent-staccato") aren't real notation articulations and aren't aliased.
const std::unordered_map<std::string, ArticulationType> LOWERCASE_ALIASES {
    { "slur", ArticulationType::Legato }, { "legato", ArticulationType::Legato },
    { "leg.", ArticulationType::Legato }, { "slurred", ArticulationType::Legato },

    { "accent", ArticulationType::Accent },
    { "staccato", ArticulationType::Staccato },
    { "staccatissimo", ArticulationType::Staccatissimo },
    { "tenuto", ArticulationType::Tenuto },
    { "marcato", ArticulationType::Marcato },
    { "soft accent", ArticulationType::SoftAccent },

    { "open", ArticulationType::Open },
    { "senza sord", ArticulationType::Open }, { "senza sord.", ArticulationType::Open },
    { "senza sordino", ArticulationType::Open },

    { "mute", ArticulationType::Mute }, { "muted", ArticulationType::Mute },
    { "con sord", ArticulationType::Mute }, { "con sord.", ArticulationType::Mute },
    { "con sordino", ArticulationType::Mute }, { "sord", ArticulationType::Mute },
    { "sord.", ArticulationType::Mute }, { "with mute", ArticulationType::Mute },
    { "stopped", ArticulationType::Mute }, { "palm mute", ArticulationType::PalmMute },

    { "harmonic", ArticulationType::Harmonic }, { "harmonics", ArticulationType::Harmonic },
    { "harm.", ArticulationType::Harmonic }, { "natural harmonic", ArticulationType::Harmonic },
    { "artificial harmonic", ArticulationType::Harmonic },

    { "up bow", ArticulationType::UpBow }, { "down bow", ArticulationType::DownBow },

    { "fade in", ArticulationType::FadeIn }, { "fade out", ArticulationType::FadeOut },
    { "volume swell", ArticulationType::VolumeSwell },
    { "snap pizzicato", ArticulationType::SnapPizzicato },

    { "trill", ArticulationType::Trill },
    { "fall", ArticulationType::Fall }, { "doit", ArticulationType::Doit },
    { "plop", ArticulationType::Plop }, { "scoop", ArticulationType::Scoop },
    { "slide out down", ArticulationType::SlideOutDown }, { "slide out up", ArticulationType::SlideOutUp },
    { "slide in above", ArticulationType::SlideInAbove }, { "slide in below", ArticulationType::SlideInBelow },

    { "arco", ArticulationType::Standard }, { "normal", ArticulationType::Standard },
    { "normale", ArticulationType::Standard }, { "norm.", ArticulationType::Standard },
    { "nor.", ArticulationType::Standard }, { "ordinary", ArticulationType::Standard },
    { "ord.", ArticulationType::Standard }, { "ordinario", ArticulationType::Standard },
    { "standard", ArticulationType::Standard }, { "std.", ArticulationType::Standard },

    { "pizz", ArticulationType::Pizzicato }, { "pizz.", ArticulationType::Pizzicato },
    { "pizzicato", ArticulationType::Pizzicato },

    { "tremolo", ArticulationType::Tremolo64th }, { "trem.", ArticulationType::Tremolo64th },
    { "tremolando", ArticulationType::Tremolo64th }, { "tremolo bar", ArticulationType::TremoloBar },

    { "vibrato", ArticulationType::Vibrato },

    { "col legno", ArticulationType::ColLegno }, { "col l.", ArticulationType::ColLegno },
    { "c.l.", ArticulationType::ColLegno },

    { "sul pont", ArticulationType::SulPont }, { "sul pont.", ArticulationType::SulPont },
    { "sul ponticello", ArticulationType::SulPont },

    { "sul tasto", ArticulationType::SulTasto }, { "sul tast.", ArticulationType::SulTasto },
    { "flautando", ArticulationType::SulTasto },

    { "detache", ArticulationType::Detache }, { "d\xc3\xa9tach\xc3\xa9", ArticulationType::Detache },
    { "martele", ArticulationType::Martele }, { "martel\xc3\xa9", ArticulationType::Martele },

    { "jazz tone", ArticulationType::JazzTone },
    { "distort", ArticulationType::Distortion }, { "distortion", ArticulationType::Distortion },
    { "overdrive", ArticulationType::Overdrive },
    { "breath", ArticulationType::Breath },
};

const std::vector<ArticulationType> DEFAULT_PRIORITY {
    ArticulationType::Staccato,
    ArticulationType::Staccatissimo,
    ArticulationType::Marcato,
    ArticulationType::Accent,
    ArticulationType::SoftAccent,
    ArticulationType::Tenuto,
};

std::string toLower(const std::string& s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

bool parseKeyswitchValue(const QJsonValue& value, KeyswitchNote& out)
{
    if (value.isDouble()) {
        out.pitch = static_cast<int16_t>(value.toInt(-1));
        out.velocity = 100;
        return out.isValid();
    }

    if (!value.isString()) {
        return false;
    }

    const QString str = value.toString();
    const int sep = str.indexOf('|');

    bool pitchOk = false;
    bool velOk = true;

    if (sep < 0) {
        out.pitch = static_cast<int16_t>(str.toInt(&pitchOk));
        out.velocity = 100;
    } else {
        out.pitch = static_cast<int16_t>(str.left(sep).toInt(&pitchOk));
        out.velocity = static_cast<int16_t>(str.mid(sep + 1).toInt(&velOk));
    }

    return pitchOk && velOk && out.isValid();
}
} // namespace

bool KeyswitchMapLoader::articulationTypeFromMapKey(const std::string& key, ArticulationType& out)
{
    auto canonicalIt = CANONICAL_NAMES.find(key);
    if (canonicalIt != CANONICAL_NAMES.cend()) {
        out = canonicalIt->second;
        return true;
    }

    auto aliasIt = LOWERCASE_ALIASES.find(toLower(key));
    if (aliasIt != LOWERCASE_ALIASES.cend()) {
        out = aliasIt->second;
        return true;
    }

    return false;
}

bool KeyswitchMapLoader::isKeyswitchMapObject(const QJsonObject& object)
{
    return object.value("keyswitches").isObject();
}

KeyswitchMap KeyswitchMapLoader::loadFromJson(const QJsonObject& object, const std::string& id)
{
    KeyswitchMap map;

    if (!isKeyswitchMapObject(object)) {
        return map;
    }

    map.id = id;
    map.leadMs = object.value("leadMs").toInt(5);
    map.writeEveryNote = object.value("writeEveryNote").toBool(false);

    if (object.value("default").isString()) {
        ArticulationType type = ArticulationType::Standard;
        const std::string defaultKey = object.value("default").toString().toStdString();
        if (articulationTypeFromMapKey(defaultKey, type)) {
            map.defaultType = type;
        } else {
            LOGW() << "Keyswitch map '" << id << "': unknown default technique '" << defaultKey << "'";
        }
    }

    for (const QJsonValue& entry : object.value("priority").toArray()) {
        if (!entry.isString()) {
            continue;
        }

        ArticulationType type = ArticulationType::Undefined;
        const std::string key = entry.toString().toStdString();
        if (articulationTypeFromMapKey(key, type)) {
            map.priority.push_back(type);
        } else {
            LOGW() << "Keyswitch map '" << id << "': unknown priority entry '" << key << "'";
        }
    }

    const QJsonObject keyswitches = object.value("keyswitches").toObject();
    for (auto it = keyswitches.constBegin(); it != keyswitches.constEnd(); ++it) {
        const std::string key = it.key().toStdString();

        ArticulationType type = ArticulationType::Undefined;
        if (!articulationTypeFromMapKey(key, type)) {
            LOGW() << "Keyswitch map '" << id << "': unknown articulation/technique '" << key << "'";
            continue;
        }

        KeyswitchNote note;
        if (!parseKeyswitchValue(it.value(), note)) {
            LOGW() << "Keyswitch map '" << id << "': invalid keyswitch value for '" << key << "'";
            continue;
        }

        map.keyswitches[type] = note;
    }

    return map;
}

const KeyswitchNote* KeyswitchMap::keyswitchForNote(const ArticulationMap& noteArticulations) const
{
    const std::vector<ArticulationType>& order = !priority.empty() ? priority : DEFAULT_PRIORITY;

    for (ArticulationType type : order) {
        if (!noteArticulations.contains(type)) {
            continue;
        }

        auto it = keyswitches.find(type);
        if (it != keyswitches.cend() && it->second.isValid()) {
            return &it->second;
        }
    }

    //! NOTE Not disambiguated by priority (e.g. a mapped technique the map author didn't list there):
    //! fall back to whichever single mapped articulation the note carries.
    for (const auto& pair : noteArticulations) {
        auto it = keyswitches.find(pair.first);
        if (it != keyswitches.cend() && it->second.isValid()) {
            return &it->second;
        }
    }

    auto it = keyswitches.find(defaultType);
    return it != keyswitches.cend() && it->second.isValid() ? &it->second : nullptr;
}
}
