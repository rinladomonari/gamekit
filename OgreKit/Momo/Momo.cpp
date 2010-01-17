/*
-------------------------------------------------------------------------------
	This file is part of OgreKit.
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
#include "OgreKitApplication.h"
#include "OgreBlend.h"
#include "OgreException.h"
#include "OgreColourValue.h"
#include "OgreSceneManager.h"
#include "OgreRenderWindow.h"
#include "OgreViewport.h"
#include "OgreMathUtils.h"
#include "autogenerated/blender.h"
#include "OgreManualSkeleton.h"
#include "OgreAction.h"
#include "OgreActionManager.h"
#include "bMain.h"
#include "bBlenderFile.h"

#include "btBulletDynamicsCommon.h"

using namespace Ogre;


// ----------------------------------------------------------------------------
class EditCamera
{
protected:
	SceneNode *m_roll, *m_pitch, *m_camNode;

public:
	EditCamera(Camera *parent, SceneManager *manager);
	virtual ~EditCamera() {}

	void		create(Camera *parent, SceneManager *manager);
	void		rotate(Real dx, Real dy);
	void		zoom(Real z);
	void		pan(Real dx, Real dy);
	void		center(const Vector3 &v);
	Real		getDistanceToRoot(void);
	void		setOffset(const Vector3& oloc,
	                const Quaternion &orot,
	                Real startZoom = 10,
	                const Vector3& center = Vector3::ZERO);
};



// ----------------------------------------------------------------------------
EditCamera::EditCamera(Camera *parent, SceneManager *manager) :
		m_roll(0), m_pitch(0), m_camNode(0)
{
	create(parent, manager);
}

// ----------------------------------------------------------------------------
void EditCamera::create(Camera *parent, SceneManager *manager)
{
	m_roll = manager->getRootSceneNode()->createChildSceneNode();
	m_pitch = m_roll->createChildSceneNode();
	m_camNode = m_pitch->createChildSceneNode();

	if (parent->getParentSceneNode())
	{
		SceneNode *cpn = parent->getParentSceneNode();
		cpn->detachObject(parent);
		m_camNode->attachObject(parent);

		Vector3 neul = MathUtils::getEulerFromQuat(cpn->_getDerivedOrientation());
		Vector3 zeul = Vector3(0, 0, neul.z);

		setOffset(cpn->_getDerivedPosition(), cpn->_getDerivedOrientation(), 2, cpn->_getDerivedPosition());
	}
	else
	{
		setOffset(Vector3(0, -10, 0), MathUtils::getQuatFromEuler(Vector3(90, 0, 0)));
		m_camNode->attachObject(parent);
	}

}

// ----------------------------------------------------------------------------
void EditCamera::setOffset(const Vector3& oloc, const Quaternion &orot, Real startZoom, const Vector3& cent)
{
	Vector3 neul = MathUtils::getEulerFromQuat(orot);
	m_roll->setPosition(oloc);
	m_camNode->setOrientation(MathUtils::getQuatFromEuler(Vector3(90, 0, 0)));

	m_roll->setOrientation(MathUtils::getQuatFromEuler(Vector3(0, 0, neul.z)));
	if (startZoom == 0)
		startZoom = 0.01f;

	zoom(startZoom);
	center(cent);

}

// ----------------------------------------------------------------------------
void EditCamera::center(const Vector3 &v)
{
	if (m_roll)
		m_roll->setPosition(v);
}


// ----------------------------------------------------------------------------
Real EditCamera::getDistanceToRoot()
{
	if (m_pitch)
		return m_camNode->_getDerivedPosition().distance(m_pitch->_getDerivedPosition());
	return Real(0.0);
}

// ----------------------------------------------------------------------------
void EditCamera::zoom(Real z)
{
	Vector3 d(0, 0, z);
	d = m_camNode->getOrientation() * d;
	m_camNode->translate(d);
}


// ----------------------------------------------------------------------------
void EditCamera::pan(Real dx, Real dy)
{
	if (m_roll && m_pitch)
	{
		Quaternion p = m_pitch->getOrientation();
		Quaternion r = m_roll->getOrientation();
		Quaternion c = m_camNode->getOrientation();
		Vector3 v = (r * p * c) * Vector3(dx, dy, Real(0.0));
		m_roll->translate(v);
	}
}


// ----------------------------------------------------------------------------
void EditCamera::rotate(Real dx, Real dy)
{
	if (m_roll && m_pitch)
	{
		m_roll->roll(Radian(dx));
		m_pitch->pitch(Radian(dy));

		Ogre::Vector3 p90 = Ogre::MathUtils::getEulerFromQuat(m_pitch->getOrientation());
		bool clamp = false;
		if (p90.x > 90)
		{
			clamp = true;
			p90.x = 90;
		}

		if (p90.x < -90)
		{
			clamp = true;
			p90.x = -90;
		}
		if (clamp) m_pitch->setOrientation(Ogre::MathUtils::getQuatFromEuler(p90));

	}
}


// ----------------------------------------------------------------------------
enum MomoActions
{
	Momo_Carry,
	Momo_Catch,
	Momo_Death,
	Momo_DieLava,
	Momo_dj,
	Momo_Drowning,
	Momo_EdgeClimb,
	Momo_EdgeIdle,
	Momo_Fall,
	Momo_FallUp,
	Momo_Glide,
	Momo_Hit_Lightly,
	Momo_HitCarry,
	Momo_Idle1,
	Momo_IdleCapoeira,
	Momo_IdleNasty,
	Momo_Jump,
	Momo_Kick,
	Momo_Revive,
	Momo_Run,
	Momo_RunFaster,
	Momo_ShimmyL,
	Momo_ShimmyR,
	Momo_TailWhip,
	Momo_Throw1,
	Momo_ThrowSheep,
	Momo_ThrowWith,
	Momo_ThrowWithout,
	Momo_TurnL,
	Momo_TurnR,
	Momo_Walk,
	Momo_WalkBack,
	Momo_WalkFast,
	Momo_WalkHand,
	Momo_WalkSlow,
	Momo_WallFlip,
	Momo_MAX,
};

#define FAction(x) getAction(actions, #x)


// ----------------------------------------------------------------------------
class MomoApp : public OgreKitApplication, public OgreBlend
{
protected:

	EditCamera *m_editCam;
	Viewport *m_viewport;

	ManualSkeleton*			m_momo;
	Action*					m_actions[Momo_MAX];
	ActionManager*			m_actManager;
	bool					m_cache;
	Entity*					m_momoOb;
	int						m_curAct;

	void					buildAllActions(bParse::bListBasePtr *actions);
	void					setupMomo(void);
	Blender::bAction*		findAction(bParse::bListBasePtr *actions, const char *act);
	Action*					getAction(bParse::bListBasePtr *actions, const char *actname);


public:
	MomoApp();
	virtual ~MomoApp();

	void createScene(void);
	void update(Real tick);
	void updateCamera(void);
	void endFrame(void);
};

// ----------------------------------------------------------------------------
MomoApp::MomoApp() :
		m_editCam(0), m_viewport(0),
		m_momo(0), m_actManager(0), m_cache(false), m_momoOb(0), m_curAct(0)
{
}

// ----------------------------------------------------------------------------
MomoApp::~MomoApp()
{
	delete m_momo;
	delete m_actManager;
	delete m_editCam;
}

// ----------------------------------------------------------------------------
void MomoApp::createScene(void)
{
	read("MomoAnimation.blend");
	convertAllObjects();

	if (!m_camera)
		m_camera = m_manager->createCamera("NoCamera");

	m_viewport = m_window->addViewport(m_camera);
	if (m_blenScene->world)
	{
		Blender::World *wo = m_blenScene->world;
		m_viewport->setBackgroundColour(ColourValue(wo->horr, wo->horg, wo->horb));
	}

	m_camera->setAspectRatio((Real)m_viewport->getActualWidth() / (Real)m_viewport->getActualHeight());
	m_editCam = new EditCamera(m_camera, m_manager);
	m_editCam->center(Vector3::ZERO);
	setupMomo();

}

// ----------------------------------------------------------------------------
void MomoApp::update(Real tick)
{
	updateCamera();


	static const Real scale = (27.f / 60.f);
	if (m_keyboard.isKeyDown(KC_SPACEKEY) && !m_cache)
	{
		m_cache = true;
		m_curAct ++;

		if (m_curAct >= Momo_MAX)
			m_curAct = 0;

		if (m_curAct >= 0 && m_curAct < Momo_MAX)
		{
			if (m_actions[m_curAct])
				m_actManager->setAction(m_actions[m_curAct]);
		}
	}
	else if (m_cache && !m_keyboard.isKeyDown(KC_SPACEKEY))
		m_cache = false;

	m_actManager->update(scale);
}

// ----------------------------------------------------------------------------
void MomoApp::updateCamera(void)
{


	bool shift = m_keyboard.isKeyDown(KC_LEFTSHIFTKEY);
	bool ctrl = m_keyboard.isKeyDown(KC_LEFTCTRLKEY);
	bool middledown = m_mouse.isButtonDown(OgreKitMouse::Middle);

	bool drag = m_mouse.mouseMoved();

	Real size = (Real)OgreMin(m_viewport->getActualWidth(), m_viewport->getActualHeight());

	/// arcball type
	if (drag && middledown && !ctrl && !shift)
	{
		Real dx = 2.f * (-(m_mouse.relitave.x)) * (2.f / size);
		Real dy = 2.f * (-(m_mouse.relitave.y)) * (2.f / size);

		m_editCam->rotate(dx, dy);
	}
	/// zoom

	else if (drag && middledown && ctrl && !shift)
	{
		Real zfac = 2.f * m_editCam->getDistanceToRoot();
		Real dy = 2.f * ((m_mouse.relitave.y)) * (zfac / size);
		m_editCam->zoom(dy);
	}

	/// pan
	else if (drag && middledown && !ctrl && shift)
	{
		Real zfac = m_editCam->getDistanceToRoot();

		Real dx = 2.f * (-(m_mouse.relitave.x)) * (zfac / size);
		Real dy = 2.f * ( (m_mouse.relitave.y)) * (zfac / size);

		m_editCam->pan(dx, dy);
	}
	else if (m_mouse.wheelDelta != 0)
	{

		Real zfac = m_editCam->getDistanceToRoot();
		Real dy = ((m_mouse.wheelDelta > 0 ? -120 : 120) * (zfac / size));
		m_editCam->zoom(dy);
	}

	if (m_keyboard.isKeyDown(KC_PADPERIOD))
		m_editCam->center(Vector3::ZERO);
}

// ----------------------------------------------------------------------------
void MomoApp::buildAllActions(bParse::bListBasePtr* actions)
{
	m_actions[Momo_Carry] = FAction(Momo_Carry);
	m_actions[Momo_Catch] = FAction(Momo_Catch);
	m_actions[Momo_Death] = FAction(Momo_Death);
	m_actions[Momo_DieLava] = FAction(Momo_DieLava);
	m_actions[Momo_dj] = FAction(Momo_dj);
	m_actions[Momo_Drowning] = FAction(Momo_Drowning);
	m_actions[Momo_EdgeClimb] = FAction(Momo_EdgeClimb);
	m_actions[Momo_EdgeIdle] = FAction(Momo_EdgeIdle);
	m_actions[Momo_Fall] = FAction(Momo_Fall);
	m_actions[Momo_FallUp] = FAction(Momo_FallUp);
	m_actions[Momo_Glide] = FAction(Momo_Glide);
	m_actions[Momo_Hit_Lightly] = FAction(Momo_Hit_Lightly);
	m_actions[Momo_HitCarry] = FAction(Momo_HitCarry);
	m_actions[Momo_Idle1] = FAction(Momo_Idle1);
	m_actions[Momo_IdleCapoeira] = FAction(Momo_IdleCapoeira);
	m_actions[Momo_IdleNasty] = FAction(Momo_IdleNasty);
	m_actions[Momo_Jump] = FAction(Momo_Jump);
	m_actions[Momo_Kick] = FAction(Momo_Kick);
	m_actions[Momo_Revive] = FAction(Momo_Revive);
	m_actions[Momo_Run] = FAction(Momo_Run);
	m_actions[Momo_RunFaster] = FAction(Momo_RunFaster);
	m_actions[Momo_ShimmyL] = FAction(Momo_ShimmyL);
	m_actions[Momo_ShimmyR] = FAction(Momo_ShimmyR);
	m_actions[Momo_TailWhip] = FAction(Momo_TailWhip);
	m_actions[Momo_Throw1] = FAction(Momo_Throw1);
	m_actions[Momo_ThrowSheep] = FAction(Momo_ThrowSheep);
	m_actions[Momo_ThrowWith] = FAction(Momo_ThrowWith);
	m_actions[Momo_ThrowWithout] = FAction(Momo_ThrowWithout);
	m_actions[Momo_TurnL] = FAction(Momo_Turn.L);
	m_actions[Momo_TurnR] = FAction(Momo_Turn.R);
	m_actions[Momo_Walk] = FAction(Momo_Walk);
	m_actions[Momo_WalkBack] = FAction(Momo_WalkBack);
	m_actions[Momo_WalkFast] = FAction(Momo_WalkFast);
	m_actions[Momo_WalkHand] = FAction(Momo_WalkHand);
	m_actions[Momo_WalkSlow] = FAction(Momo_WalkSlow);
	m_actions[Momo_WallFlip] = FAction(Momo_WallFlip);

}
// ----------------------------------------------------------------------------
void MomoApp::setupMomo(void)
{
	m_momoOb = m_manager->getEntity("MeshMomo");
	if (!m_momoOb)
	{
		OGRE_EXCEPT(Exception::ERR_INVALID_STATE,
		            "missing Momo mesh",
		            "MomoApp::setupMomo");
	}

	bParse::bListBasePtr* actions  = m_blendFile->getMain()->getAction();
	m_momo = new ManualSkeleton(m_momoOb);
	buildAllActions(actions);

	m_actManager = new ActionManager();
	m_actManager->setAction(m_actions[0]);
}

// ----------------------------------------------------------------------------
Blender::bAction *MomoApp::findAction(bParse::bListBasePtr* actions, const char *act)
{
	int i = 0;
	for (; i < actions->size(); ++i)
	{
		Blender::bAction *bAct = (Blender::bAction*)actions->at(i);
		if (!strcmp(bAct->id.name + 2, act))
		{
			return bAct;
		}
	}
	return 0;
}

// ----------------------------------------------------------------------------
Action *MomoApp::getAction(bParse::bListBasePtr* actions, const char *actname)
{
	Blender::bAction *act = findAction(actions, actname);
	if (act)
	{
		Action* ret = m_momo->createAction(act);
		if (ret)
		{
			ret->setBlendFrames(60);
			ret->setWeight(1);
			ret->setTimePosition(0);
		}
		return ret;
	}
	return 0;
}

// ----------------------------------------------------------------------------
void MomoApp::endFrame(void)
{
	if (m_keyboard.isKeyDown(KC_QKEY))
		m_quit = true;
}


// ----------------------------------------------------------------------------
int main(int argc, char **argv)
{
	MomoApp app;
	try
	{
		app.go();
	}
	catch (Exception &e)
	{
		printf("%s\n", e.getDescription().c_str());
	}
	return 0;
}