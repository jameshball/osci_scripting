/*
  ==============================================================================

   This file is part of the osci-render scripting module
   Copyright (c) 2026 James H Ball

  ==============================================================================
*/

#pragma once

#include "osci_scripting_config.h"

/*******************************************************************************
 The block below describes the properties of this module, and is read by
 the Projucer to automatically generate project code that uses it.

 BEGIN_JUCE_MODULE_DECLARATION

  ID:                osci_scripting
  vendor:            jameshball
  version:           1.0.0
  name:              osci scripting
  description:       Lua scripting support for osci apps
  website:           https://osci-render.com
  license:           GPLv3
  minimumCppStandard: 20

  dependencies:      juce_core, juce_gui_basics, juce_gui_extra, osci_render_core, osci_gui

 END_JUCE_MODULE_DECLARATION

*******************************************************************************/

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <osci_render_core/osci_render_core.h>
#include <osci_gui/osci_gui.h>

#include "lua/osci_LuaLibrary.h"
#include "lua/osci_LuaParser.h"
#include "effects/osci_LuaEffectState.h"
#include "effects/osci_CustomEffect.h"
#include "gui/osci_LuaScriptEditorModel.h"
#include "gui/osci_LuaScriptEditorComponent.h"
