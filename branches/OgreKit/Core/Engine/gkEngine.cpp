/*
-------------------------------------------------------------------------------
	This file is part of the Ogre GameKit port.
	http://gamekit.googlecode.com/

	Copyright (c) 2009 Charlie C.
-------------------------------------------------------------------------------
 This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
	 claim that you wrote the original software. If you use this software
	 in a product, an acknowledgment in the product documentation would be
	 appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
	 misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
-------------------------------------------------------------------------------
*/
#include "OgreRoot.h"
#include "OgreConfigFile.h"
#include "OgreRenderSystem.h"
#include "OgreStringConverter.h"
#include "OgreFrameListener.h"

#include "gkEngine.h"
#include "gkWindowSystem.h"
#include "gkSceneObject.h"
#include "gkSceneObjectManager.h"
#include "gkLogger.h"
#include "gkUtils.h"
#include "gkScriptManager.h"
#include "gkSoundManager.h"
#include "gkLogicManager.h"
#include "gkConsole.h"
#include "gkDebugPage.h"


#include "Loaders/gkBlendFile.h"
#include "Loaders/gkBlendLoader.h"

#include "gkRenderFactory.h"
#include "gkUserDefs.h"


#include "LinearMath/btQuickprof.h"

using namespace Ogre;




// ----------------------------------------------------------------------------
struct TickState
{
	unsigned long ticks, rate;
	unsigned long skip, loop;
	unsigned long cur, next, prev, allot;
	Real blend, fixed, invt;
	btClock *T;
	bool lock, init;
};

// ----------------------------------------------------------------------------
class gkEnginePrivate : public FrameListener
{
public:
	gkEnginePrivate();
	~gkEnginePrivate();


	/// one full update
	void tick(Real delta, bool smooth);
	bool frameRenderingQueued(const FrameEvent& evt);

	gkWindowSystem*				windowsystem;   // current window system
	gkSceneObject*				scene;			// current scene
	gkRenderFactoryPrivate*		plugin_factory; // static/dynamic loading
	TickState					state;
	Ogre::Root*					root;
};







// ----------------------------------------------------------------------------
gkEnginePrivate::gkEnginePrivate() :
		windowsystem(0),
		scene(0)
{
	memset(&state, 0, sizeof(TickState));
	plugin_factory= new gkRenderFactoryPrivate();
}

// ----------------------------------------------------------------------------
gkEnginePrivate::~gkEnginePrivate()
{
	delete plugin_factory;
}

// ----------------------------------------------------------------------------
gkEngine::gkEngine(const String& homeDir) :
		mRoot(0),
		mInitialized(false),
		mWindow(0)
{
	mPrivate= new gkEnginePrivate();
	mDefs= new gkUserDefs();
}


// ----------------------------------------------------------------------------
gkEngine::~gkEngine()
{
	if (mInitialized)
		finalize();

	gkLogger::disable();
	delete mDefs;
	mDefs= 0;
}


// ----------------------------------------------------------------------------
void gkEngine::initialize(bool autoCreateWindow)
{
	if (mInitialized)
		return;

	gkUserDefs &defs= getUserDefs();
	gkLogger::enable(defs.log, defs.verbose);

	if (defs.rendersystem == OGRE_RS_UNKNOWN)
	{
		gkPrintf("Unknown rendersystem!");
		return;
	}

	mRoot= new Root("", "");

	new gkSceneObjectManager();
	new gkLogicManager();
	new gkBlendLoader();

	gkScriptManager::initialize();

	mPrivate->plugin_factory->createRenderSystem(mRoot, defs.rendersystem);

	const RenderSystemList &renderers= mRoot->getAvailableRenderers();
	if (renderers.empty())
	{
		OGRE_EXCEPT(Exception::ERR_INVALID_STATE,
		            "No rendersystems present",
		            "gkEngine::initialize");
	}

	mRoot->setRenderSystem(renderers[0]);
	mRoot->initialise(false);

	gkWindowSystem::WindowBackend backend= gkWindowSystem::OGRE_BACKEND;

	gkWindowSystem *sys= mPrivate->windowsystem= gkWindowSystem::initialize(backend);
	if (!sys)
	{
		OGRE_EXCEPT(Exception::ERR_INVALID_STATE,
		            "gkWindowSystem creation failed",
		            "gkEngine::initialize");
	}

	if (autoCreateWindow)
		initializeWindow(defs.wintitle, (int)defs.winsize.x, (int)defs.winsize.y, defs.fullscreen);


	mAnimRate= defs.animspeed;
	mTickRate= defs.tickrate;
	mTickRate= gkClamp(mTickRate, 25, 90);

	mInitialized= true;
}

// ----------------------------------------------------------------------------
void gkEngine::initializeWindow(const Ogre::String& windowName, int w, int h, bool fullscreen)
{
	if (mPrivate->windowsystem && !mWindow)
	{
		gkWindowSystem *sys = mPrivate->windowsystem;
		mWindow = sys->createWindow(windowName, w, h, fullscreen);

		gkUserDefs &defs= getUserDefs();
		if (!defs.resources.empty())
			loadResources(defs.resources);
	}
}

// ----------------------------------------------------------------------------
void gkEngine::finalize()
{
	if (!mInitialized) return;

	delete gkLogicManager::getSingletonPtr();
	gkWindowSystem::finalize();
	gkScriptManager::finalize();

	delete gkSceneObjectManager::getSingletonPtr();
	delete gkBlendLoader::getSingletonPtr();

	delete mRoot;
	delete mPrivate;

	mRoot= 0;
	mInitialized= false;
}

// ----------------------------------------------------------------------------
gkUserDefs& gkEngine::getUserDefs(void)
{
	GK_ASSERT(mDefs);
	return *mDefs;
}

// ----------------------------------------------------------------------------
void gkEngine::requestExit(void)
{
	gkWindowSystem::getSingleton().exit();
}

// ----------------------------------------------------------------------------
gkBlendFile* gkEngine::loadBlendFile(const String& blend, const String& inResource)
{
	gkBlendFile *file= 0;
	try
	{
		file= gkBlendLoader::getSingleton().loadFile(blend, inResource);
	}
	catch (Ogre::Exception &e)
	{
		gkPrintf("%s", e.getDescription().c_str());
	}
	return file;
}

// ----------------------------------------------------------------------------
void gkEngine::loadResources(const String &name)
{
	if (name.empty())
		return;

	try
	{
		ConfigFile fp;
		fp.load(name);

		ResourceGroupManager *resourceManager= ResourceGroupManager::getSingletonPtr();
		ConfigFile::SectionIterator cit= fp.getSectionIterator();

		while (cit.hasMoreElements())
		{
			String name= cit.peekNextKey();
			ConfigFile::SettingsMultiMap *ptr= cit.getNext();
			for (ConfigFile::SettingsMultiMap::iterator dit= ptr->begin(); dit != ptr->end(); ++dit)
				resourceManager->addResourceLocation(dit->second, dit->first, name);
		}
		ResourceGroupManager::getSingleton().initialiseAllResourceGroups();
	}
	catch (Exception &e)
	{
		gkLogMessage("Failed to load resource file!\n" << e.getDescription());
	}
}


// ----------------------------------------------------------------------------
void gkEngine::addDebugProperty(gkVariable *prop)
{
}

// ----------------------------------------------------------------------------
void gkEngine::removeDebugProperty(gkVariable *prop)
{
}

// ----------------------------------------------------------------------------
#define ENGINE_TICKS_PER_SECOND Real(60)
#define ENGINE_TIME_SCALE		Real(0.001)
#define GET_TICK(t) ((unsigned long)(t)->getTimeMilliseconds())

// tick states
Real gkEngine::mTickRate= ENGINE_TICKS_PER_SECOND;
Real gkEngine::mAnimRate= 25;


// ----------------------------------------------------------------------------
Real gkEngine::getStepRate(void)
{
	return Real(1.0) / ENGINE_TICKS_PER_SECOND;
}

// ----------------------------------------------------------------------------
Real gkEngine::getTickRate(void)
{
	return ENGINE_TICKS_PER_SECOND;
}

// ----------------------------------------------------------------------------
Real gkEngine::getAnimRate(void)
{
	return mAnimRate;
}


// ----------------------------------------------------------------------------
void gkEngine::run(void)
{

	GK_ASSERT(mPrivate);
	if (!mPrivate->scene)
	{
		gkLogMessage("Can't run with out a registered scene. exiting");
		return;
	}

	gkWindowSystem *sys= mPrivate->windowsystem;
	if (!sys)
	{
		gkLogMessage("Can't run with out a window system. exiting");
		return;
	}

	// setup timer
	mRoot->clearEventTimes();
	mRoot->getRenderSystem()->_initRenderTargets();
	mRoot->addFrameListener(mPrivate);

	btClock t; t.reset();

	TickState state;
	state.rate = ENGINE_TICKS_PER_SECOND;
	state.ticks = 1000/state.rate;
	state.skip  = gkMax(state.rate/5, 1); 
	state.invt  = 1.0 / state.ticks;
	state.fixed = 1.0 / ENGINE_TICKS_PER_SECOND;
	state.T = &t;
	state.init = false;
	mPrivate->state = state;
	mPrivate->root = mRoot;

	do
	{
		sys->processEvents();
		mRoot->renderOneFrame();
	}
	while (!sys->exitRequest());

	mRoot->removeFrameListener(mPrivate);
}

// ----------------------------------------------------------------------------
bool gkEnginePrivate::frameRenderingQueued(const FrameEvent& evt)
{
	state.loop = 0;
	state.lock = false;

	if (!state.init)
	{
		// initialize timer states
		state.init = true;
		state.cur = GET_TICK(state.T);
		state.next = state.prev = state.cur;
	}

	while ((state.cur = GET_TICK(state.T)) > state.next && state.loop < state.skip)
	{
		if (!state.lock) tick(state.fixed, true);
		if (((GET_TICK(state.T) - state.cur) * ENGINE_TIME_SCALE) > state.fixed)
			state.lock = true;

		state.next += state.ticks;
		++state.loop;
	}

	state.blend = Ogre::Real(GET_TICK(state.T) + state.ticks - state.next) * state.invt;
	if (state.blend >= 0 && state.blend <= 1)
		scene->synchronizeMotion(1.0, state.blend);
	return true;
}

// ----------------------------------------------------------------------------
void gkEnginePrivate::tick(Real dt, bool smooth)
{
	/// Proccess one full game tick
	GK_ASSERT(windowsystem);
	GK_ASSERT(scene);

	/// dispatch inputs
	windowsystem->dispatchEvents();

	/// update main scene
	scene->update(dt, state.fixed, smooth);

	/// clear per frame stuff
	windowsystem->endFrame();
}

// ----------------------------------------------------------------------------
void gkEngine::setActiveScene(gkSceneObject *sc)
{
	GK_ASSERT(mPrivate);
	mPrivate->scene= sc;
}

// ----------------------------------------------------------------------------
GK_IMPLEMENT_SINGLETON(gkEngine);
