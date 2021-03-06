/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef QGBA_GB_OVERRIDE
#define QGBA_GB_OVERRIDE

#include "Override.h"

extern "C" {
#include "gb/overrides.h"
}

namespace QGBA {

class GBOverride : public Override {
public:
	void apply(struct mCore*) override;
	void save(struct Configuration*) const override;

	struct GBCartridgeOverride override;
};

}

#endif
