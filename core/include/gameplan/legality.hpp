// FTC legality evaluation against the active season package (never hard-coded for all seasons).
// Status: competition-legal | experimental (non-standard but physically plausible overrides) |
// illegal (rule violation or deliberately unrealistic values). A robot that simulates fine is
// still flagged if it breaks a rule.
#pragma once
#include <string>
#include <vector>

#include "gameplan/electrical.hpp"
#include "gameplan/json.hpp"
#include "gameplan/robot.hpp"
#include "gameplan/season.hpp"

namespace gp {

struct LegalityIssue {
    std::string rule, severity, message;   // severity: illegal | experimental | warning | info
};

struct LegalityResult {
    std::string status = "competition-legal";
    std::vector<LegalityIssue> issues;
    Json toJson() const;
};

LegalityResult checkLegality(const RobotConfig& rc, const SeasonDef& season, const Components& comps);

}  // namespace gp
