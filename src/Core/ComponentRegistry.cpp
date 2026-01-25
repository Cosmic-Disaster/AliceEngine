#include "Core/ComponentRegistry.h"
#include "Core/World.h"
#include "Core/EditorComponentRegistry.h"
#include "Core/JsonRttr.h"
#include "Core/SceneFile.h"
#include "Core/SceneSerializationHelpers.h"
#include "Logger.h"

#include <rttr/registration>
#include <DirectXMath.h>

// 컴포넌트 헤더들
#include "Components/TransformComponent.h"
#include "Components/MaterialComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkinnedAnimationComponent.h"
#include "Components/CameraComponent.h"
#include "Components/CameraFollowComponent.h"
#include "Components/CameraSpringArmComponent.h"
#include "Components/CameraLookAtComponent.h"
#include "Components/CameraShakeComponent.h"
#include "Components/CameraBlendComponent.h"
#include "Components/CameraInputComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/ComputeEffectComponent.h"
#include "Components/EffectComponent.h"
#include "Components/TrailEffectComponent.h"
#include "Components/AudioListenerComponent.h"
#include "Components/AudioSourceComponent.h"
#include "Components/SoundBoxComponent.h"

// 물리 컴포넌트 헤더
#include "PhysX/Components/Phy_RigidBodyComponent.h"
#include "PhysX/Components/Phy_ColliderComponent.h"
#include "PhysX/Components/Phy_MeshColliderComponent.h"
#include "PhysX/Components/Phy_TerrainHeightFieldComponent.h"
#include "PhysX/Components/Phy_CCTComponent.h"
#include "PhysX/Components/Phy_SettingsComponent.h"
#include "PhysX/Components/Phy_JointComponent.h"
#include "PhysX/IPhysicsWorld.h"
#include "Core/Material.h"

// UI 컴포넌트 헤더
#include "UI/UITransform.h"
#include "UI/UI_ImageComponent.h"
#include "UI/UI_ScriptComponent.h"
#include "UI/IUIScript.h"

using namespace DirectX;

namespace Alice
{
	void LinkComponentRegistry() {
        ALICE_LOG_INFO("[리플렉션] rttr Success");
    }

    RTTR_REGISTRATION
    {
        // === DirectX 타입 등록 ===
        rttr::registration::class_<XMFLOAT2>("XMFLOAT2")
            .constructor<>()
            .property("x", &XMFLOAT2::x)
            .property("y", &XMFLOAT2::y);

        rttr::registration::class_<XMFLOAT3>("XMFLOAT3")
            .constructor<>()
            .property("x", &XMFLOAT3::x)
            .property("y", &XMFLOAT3::y)
            .property("z", &XMFLOAT3::z);

        // XMFLOAT4X4는 4x4 행렬을 나타내는 타입
        // 렌더링할 때 4x4 행렬을 렌더링하기 위해 등록
        rttr::registration::class_<XMFLOAT4X4>("XMFLOAT4X4")
            .constructor<>()
            .property("_11", &XMFLOAT4X4::_11)
            .property("_12", &XMFLOAT4X4::_12)
            .property("_13", &XMFLOAT4X4::_13)
            .property("_14", &XMFLOAT4X4::_14)
            .property("_21", &XMFLOAT4X4::_21)
            .property("_22", &XMFLOAT4X4::_22)
            .property("_23", &XMFLOAT4X4::_23)
            .property("_24", &XMFLOAT4X4::_24)
            .property("_31", &XMFLOAT4X4::_31)
            .property("_32", &XMFLOAT4X4::_32)
            .property("_33", &XMFLOAT4X4::_33)
            .property("_34", &XMFLOAT4X4::_34)
            .property("_41", &XMFLOAT4X4::_41)
            .property("_42", &XMFLOAT4X4::_42)
            .property("_43", &XMFLOAT4X4::_43)
            .property("_44", &XMFLOAT4X4::_44);

        rttr::registration::enumeration<AudioType>("AudioType")
        (
            rttr::value("BGM", AudioType::BGM),
            rttr::value("SFX", AudioType::SFX)
        );

        rttr::registration::enumeration<SoundBoxType>("SoundBoxType")
        (
            rttr::value("BGM", SoundBoxType::BGM),
            rttr::value("SFX", SoundBoxType::SFX)
        );

        // === TransformComponent 등록 ===
        rttr::registration::class_<TransformComponent>("TransformComponent")
            .constructor<>()
            .property("position", &TransformComponent::position)
            .property("rotation", &TransformComponent::rotation)
            .property("scale", &TransformComponent::scale)
            .property("enabled", &TransformComponent::enabled);

        // === MaterialComponent 등록 ===
        rttr::registration::class_<MaterialComponent>("MaterialComponent")
            .constructor<>()
            .property("color", &MaterialComponent::color)
            .property("roughness", &MaterialComponent::roughness)
            .property("metalness", &MaterialComponent::metalness)
            .property("shadingMode", &MaterialComponent::shadingMode)
            .property("assetPath", &MaterialComponent::assetPath)
            .property("albedoTexturePath", &MaterialComponent::albedoTexturePath)
            .property("normalStrength", &MaterialComponent::normalStrength)
            .property("outlineColor", &MaterialComponent::outlineColor)
            .property("outlineWidth", &MaterialComponent::outlineWidth);

        // === SkinnedMeshComponent 등록 ===
        // boneMatrices는 뼈 행렬을 나타내는 프로퍼티
        rttr::registration::class_<SkinnedMeshComponent>("SkinnedMeshComponent")
            .constructor<>()
            .property("meshAssetPath", &SkinnedMeshComponent::meshAssetPath)
            .property("instanceAssetPath", &SkinnedMeshComponent::instanceAssetPath)
            .property("boneCount", &SkinnedMeshComponent::boneCount);

        // === SkinnedAnimationComponent 등록 ===
        rttr::registration::class_<SkinnedAnimationComponent>("SkinnedAnimationComponent")
            .constructor<>()
            .property("clipIndex", &SkinnedAnimationComponent::clipIndex)
            .property("playing", &SkinnedAnimationComponent::playing)
            .property("speed", &SkinnedAnimationComponent::speed)
            .property("timeSec", &SkinnedAnimationComponent::timeSec);
        
        // palette는 팔레트를 나타내는 프로퍼티

        // === AdvancedAnimationComponent 등록 ===
        rttr::registration::class_<AdvancedAnimLayer>("AdvancedAnimLayer")
            .constructor<>()
            .property("enabled", &AdvancedAnimLayer::enabled)
            .property("autoAdvance", &AdvancedAnimLayer::autoAdvance)
            .property("clipA", &AdvancedAnimLayer::clipA)
            .property("clipB", &AdvancedAnimLayer::clipB)
            .property("timeA", &AdvancedAnimLayer::timeA)
            .property("timeB", &AdvancedAnimLayer::timeB)
            .property("speedA", &AdvancedAnimLayer::speedA)
            .property("speedB", &AdvancedAnimLayer::speedB)
            .property("loopA", &AdvancedAnimLayer::loopA)
            .property("loopB", &AdvancedAnimLayer::loopB)
            .property("blend01", &AdvancedAnimLayer::blend01)
            .property("layerAlpha", &AdvancedAnimLayer::layerAlpha);

        rttr::registration::class_<AdvancedAnimAdditive>("AdvancedAnimAdditive")
            .constructor<>()
            .property("enabled", &AdvancedAnimAdditive::enabled)
            .property("autoAdvance", &AdvancedAnimAdditive::autoAdvance)
            .property("clip", &AdvancedAnimAdditive::clip)
            .property("refClip", &AdvancedAnimAdditive::refClip)
            .property("time", &AdvancedAnimAdditive::time)
            .property("speed", &AdvancedAnimAdditive::speed)
            .property("loop", &AdvancedAnimAdditive::loop)
            .property("alpha", &AdvancedAnimAdditive::alpha);

        rttr::registration::class_<AdvancedAnimProcedural>("AdvancedAnimProcedural")
            .constructor<>()
            .property("strength", &AdvancedAnimProcedural::strength)
            .property("seed", &AdvancedAnimProcedural::seed)
            .property("timeSec", &AdvancedAnimProcedural::timeSec);

        rttr::registration::class_<AdvancedAnimIK>("AdvancedAnimIK")
            .constructor<>()
            .property("enabled", &AdvancedAnimIK::enabled)
            .property("tipBone", &AdvancedAnimIK::tipBone)
            .property("chainLength", &AdvancedAnimIK::chainLength)
            .property("targetMS", &AdvancedAnimIK::targetMS)
            .property("weight", &AdvancedAnimIK::weight);

        rttr::registration::class_<AdvancedAnimAim>("AdvancedAnimAim")
            .constructor<>()
            .property("enabled", &AdvancedAnimAim::enabled)
            .property("yawRad", &AdvancedAnimAim::yawRad)
            .property("weight", &AdvancedAnimAim::weight);

        rttr::registration::class_<AdvancedAnimSocket>("AdvancedAnimSocket")
            .constructor<>()
            .property("name", &AdvancedAnimSocket::name)
            .property("parentBone", &AdvancedAnimSocket::parentBone)
            .property("pos", &AdvancedAnimSocket::pos)
            .property("rotDeg", &AdvancedAnimSocket::rotDeg)
            .property("scale", &AdvancedAnimSocket::scale);

        rttr::registration::class_<AdvancedAnimationComponent>("AdvancedAnimationComponent")
            .constructor<>()
            .property("enabled", &AdvancedAnimationComponent::enabled)
            .property("playing", &AdvancedAnimationComponent::playing)
            .property("base", &AdvancedAnimationComponent::base)
            .property("upper", &AdvancedAnimationComponent::upper)
            .property("additive", &AdvancedAnimationComponent::additive)
            .property("procedural", &AdvancedAnimationComponent::procedural)
            .property("ik", &AdvancedAnimationComponent::ik)
            .property("ikChains", &AdvancedAnimationComponent::ikChains)
            .property("aim", &AdvancedAnimationComponent::aim)
            .property("sockets", &AdvancedAnimationComponent::sockets);


		//  Enum 등록
		rttr::registration::enumeration<SoundBoxType>("alice_SoundBoxType")
			(
				rttr::value("BGM", SoundBoxType::BGM),
				rttr::value("SFX", SoundBoxType::SFX)
				);

		rttr::registration::enumeration<AudioType>("alice_AudioType")
			(
				rttr::value("BGM", AudioType::BGM),
				rttr::value("SFX", AudioType::SFX)
				);

		//  SoundBoxComponent 등록
		rttr::registration::class_<SoundBoxComponent>("SoundBoxComponent")
			.constructor<>()
			.property("soundKey", &SoundBoxComponent::soundKey)
			.property("soundPath", &SoundBoxComponent::soundPath)
			.property("type", &SoundBoxComponent::type)
			.property("loop", &SoundBoxComponent::loop)
			.property("playOnEnter", &SoundBoxComponent::playOnEnter)
			.property("stopOnExit", &SoundBoxComponent::stopOnExit)
			.property("boundsMin", &SoundBoxComponent::boundsMin)
			.property("boundsMax", &SoundBoxComponent::boundsMax)
			.property("edgeVolume", &SoundBoxComponent::edgeVolume)
			.property("centerVolume", &SoundBoxComponent::centerVolume)
			.property("curve", &SoundBoxComponent::curve)
			.property("minDistance", &SoundBoxComponent::minDistance)
			.property("maxDistance", &SoundBoxComponent::maxDistance)
			.property("debugDraw", &SoundBoxComponent::debugDraw)
			.property("targetEntity", &SoundBoxComponent::targetEntity);

		//  AudioListenerComponent 등록
		rttr::registration::class_<AudioListenerComponent>("AudioListenerComponent")
			.constructor<>()
			.property("primary", &AudioListenerComponent::primary);

		//  AudioSourceComponent 등록
		rttr::registration::class_<AudioSourceComponent>("AudioSourceComponent")
			.constructor<>()
			.property("soundKey", &AudioSourceComponent::soundKey)
			.property("soundPath", &AudioSourceComponent::soundPath)
			.property("type", &AudioSourceComponent::type)
			.property("is3D", &AudioSourceComponent::is3D)
			.property("loop", &AudioSourceComponent::loop)
			.property("playOnStart", &AudioSourceComponent::playOnStart)
			.property("volume", &AudioSourceComponent::volume)
			.property("pitch", &AudioSourceComponent::pitch)
			.property("minDistance", &AudioSourceComponent::minDistance)
			.property("maxDistance", &AudioSourceComponent::maxDistance)
			.property("requestPlay", &AudioSourceComponent::requestPlay)
			.property("requestStop", &AudioSourceComponent::requestStop)
			.property("debugDraw", &AudioSourceComponent::debugDraw);

        // === CameraComponent 등록 ===
        // Transform은 TransformComponent에서 담당하므로 여기선 제외
        rttr::registration::class_<CameraComponent>("CameraComponent")
            .constructor<>()
            .property("primary", &CameraComponent::GetPrimary, &CameraComponent::SetPrimary)
            .property("FOV", &CameraComponent::GetFov, &CameraComponent::SetFov)
                (rttr::metadata("Min", 10.0f), rttr::metadata("Max", 170.0f))
            .property("Near Plane", &CameraComponent::GetNear, &CameraComponent::SetNear)
                (rttr::metadata("Min", 0.01f))
            .property("Far Plane", &CameraComponent::GetFar, &CameraComponent::SetFar)
                (rttr::metadata("Min", 10.0f))
            .property("useAspectOverride", &CameraComponent::useAspectOverride)
            .property("aspectOverride", &CameraComponent::aspectOverride);

        // === CameraFollowComponent 등록 ===
        rttr::registration::class_<CameraFollowComponent>("CameraFollowComponent")
            .constructor<>()
            .property("enabled", &CameraFollowComponent::enabled)
            .property("targetName", &CameraFollowComponent::targetName)
            .property("heightOffset", &CameraFollowComponent::heightOffset)
            .property("shoulderOffset", &CameraFollowComponent::shoulderOffset)
            .property("shoulderSide", &CameraFollowComponent::shoulderSide)
            .property("enableInput", &CameraFollowComponent::enableInput)
            .property("sensitivity", &CameraFollowComponent::sensitivity)
            .property("yawDeg", &CameraFollowComponent::yawDeg)
            .property("pitchDeg", &CameraFollowComponent::pitchDeg)
            .property("pitchMinDeg", &CameraFollowComponent::pitchMinDeg)
            .property("pitchMaxDeg", &CameraFollowComponent::pitchMaxDeg)
            .property("baseDistance", &CameraFollowComponent::baseDistance)
            .property("minDistance", &CameraFollowComponent::minDistance)
            .property("maxDistance", &CameraFollowComponent::maxDistance)
            .property("positionDamping", &CameraFollowComponent::positionDamping)
            .property("rotationDamping", &CameraFollowComponent::rotationDamping)
            .property("fastTurnYawThresholdDeg", &CameraFollowComponent::fastTurnYawThresholdDeg)
            .property("fastTurnMultiplier", &CameraFollowComponent::fastTurnMultiplier)
            .property("mode", &CameraFollowComponent::mode)
            .property("exploreDistance", &CameraFollowComponent::exploreDistance)
            .property("combatDistance", &CameraFollowComponent::combatDistance)
            .property("lockOnDistance", &CameraFollowComponent::lockOnDistance)
            .property("aimDistance", &CameraFollowComponent::aimDistance)
            .property("bossIntroDistance", &CameraFollowComponent::bossIntroDistance)
            .property("deathDistance", &CameraFollowComponent::deathDistance)
            .property("exploreFovDeg", &CameraFollowComponent::exploreFovDeg)
            .property("combatFovDeg", &CameraFollowComponent::combatFovDeg)
            .property("lockOnFovDeg", &CameraFollowComponent::lockOnFovDeg)
            .property("aimFovDeg", &CameraFollowComponent::aimFovDeg)
            .property("bossIntroFovDeg", &CameraFollowComponent::bossIntroFovDeg)
            .property("deathFovDeg", &CameraFollowComponent::deathFovDeg)
            .property("fovDamping", &CameraFollowComponent::fovDamping)
            .property("enableLockOn", &CameraFollowComponent::enableLockOn)
            .property("lockOnMaxDistance", &CameraFollowComponent::lockOnMaxDistance)
            .property("lockOnMaxAngleDeg", &CameraFollowComponent::lockOnMaxAngleDeg)
            .property("lockOnSwitchAngleDeg", &CameraFollowComponent::lockOnSwitchAngleDeg)
            .property("lockOnAngleWeight", &CameraFollowComponent::lockOnAngleWeight)
            .property("lockOnRotationDamping", &CameraFollowComponent::lockOnRotationDamping)
            .property("allowManualOrbitInLockOn", &CameraFollowComponent::allowManualOrbitInLockOn)
            .property("cameraTimeScale", &CameraFollowComponent::cameraTimeScale);

        // === CameraSpringArmComponent 등록 ===
        rttr::registration::class_<CameraSpringArmComponent>("CameraSpringArmComponent")
            .constructor<>()
            .property("enabled", &CameraSpringArmComponent::enabled)
            .property("enableCollision", &CameraSpringArmComponent::enableCollision)
            .property("enableZoom", &CameraSpringArmComponent::enableZoom)
            .property("distance", &CameraSpringArmComponent::distance)
            .property("minDistance", &CameraSpringArmComponent::minDistance)
            .property("maxDistance", &CameraSpringArmComponent::maxDistance)
            .property("zoomSpeed", &CameraSpringArmComponent::zoomSpeed)
            .property("distanceDamping", &CameraSpringArmComponent::distanceDamping)
            .property("probeRadius", &CameraSpringArmComponent::probeRadius)
            .property("probePadding", &CameraSpringArmComponent::probePadding)
            .property("minHeight", &CameraSpringArmComponent::minHeight);

        // === CameraLookAtComponent 등록 ===
        rttr::registration::class_<CameraLookAtComponent>("CameraLookAtComponent")
            .constructor<>()
            .property("enabled", &CameraLookAtComponent::enabled)
            .property("targetName", &CameraLookAtComponent::targetName)
            .property("rotationDamping", &CameraLookAtComponent::rotationDamping);

        // === CameraShakeComponent 등록 ===
        rttr::registration::class_<CameraShakeComponent>("CameraShakeComponent")
            .constructor<>()
            .property("enabled", &CameraShakeComponent::enabled)
            .property("amplitude", &CameraShakeComponent::amplitude)
            .property("frequency", &CameraShakeComponent::frequency)
            .property("duration", &CameraShakeComponent::duration)
            .property("decay", &CameraShakeComponent::decay);

        // === CameraBlendComponent 등록 ===
        rttr::registration::class_<CameraBlendComponent>("CameraBlendComponent")
            .constructor<>()
            .property("targetName", &CameraBlendComponent::targetName)
            .property("duration", &CameraBlendComponent::duration)
            .property("useSmoothStep", &CameraBlendComponent::useSmoothStep)
            .property("slowTriggerT", &CameraBlendComponent::slowTriggerT)
            .property("slowDuration", &CameraBlendComponent::slowDuration)
            .property("slowTimeScale", &CameraBlendComponent::slowTimeScale);

        // === CameraInputComponent 등록 ===
        rttr::registration::class_<CameraInputComponent>("CameraInputComponent")
            .constructor<>()
            .property("enabled", &CameraInputComponent::enabled)
            .property("cameraListCsv", &CameraInputComponent::cameraListCsv)
            .property("blendTimeKey3", &CameraInputComponent::blendTimeKey3)
            .property("blendTimeKey4", &CameraInputComponent::blendTimeKey4)
            .property("blendTimeKey5", &CameraInputComponent::blendTimeKey5)
            .property("shakeAmplitudeKey4", &CameraInputComponent::shakeAmplitudeKey4)
            .property("shakeFrequencyKey4", &CameraInputComponent::shakeFrequencyKey4)
            .property("shakeDurationKey4", &CameraInputComponent::shakeDurationKey4)
            .property("shakeDecayKey4", &CameraInputComponent::shakeDecayKey4)
            .property("slowTriggerTKey5", &CameraInputComponent::slowTriggerTKey5)
            .property("slowDurationKey5", &CameraInputComponent::slowDurationKey5)
            .property("slowTimeScaleKey5", &CameraInputComponent::slowTimeScaleKey5)
            .property("lookAtTargetName", &CameraInputComponent::lookAtTargetName);

        // === PointLightComponent 등록 ===
        rttr::registration::class_<PointLightComponent>("PointLightComponent")
            .constructor<>()
            .property("color", &PointLightComponent::color)
            .property("intensity", &PointLightComponent::intensity)
            .property("range", &PointLightComponent::range)
            .property("enabled", &PointLightComponent::enabled);

        // === SpotLightComponent 등록 ===
        rttr::registration::class_<SpotLightComponent>("SpotLightComponent")
            .constructor<>()
            .property("color", &SpotLightComponent::color)
            .property("intensity", &SpotLightComponent::intensity)
            .property("range", &SpotLightComponent::range)
            .property("innerAngleDeg", &SpotLightComponent::innerAngleDeg)
            .property("outerAngleDeg", &SpotLightComponent::outerAngleDeg)
            .property("enabled", &SpotLightComponent::enabled);

        // === RectLightComponent 등록 ===
        rttr::registration::class_<RectLightComponent>("RectLightComponent")
            .constructor<>()
            .property("color", &RectLightComponent::color)
            .property("intensity", &RectLightComponent::intensity)
            .property("width", &RectLightComponent::width)
            .property("height", &RectLightComponent::height)
            .property("range", &RectLightComponent::range)
            .property("enabled", &RectLightComponent::enabled);

        // === ComputeEffectComponent 등록 ===
        rttr::registration::class_<ComputeEffectComponent>("ComputeEffectComponent")
            .constructor<>()
            .property("enabled", &ComputeEffectComponent::enabled)
            .property("shaderName", &ComputeEffectComponent::shaderName)
            .property("effectParams", &ComputeEffectComponent::effectParams)
            .property("intensity", &ComputeEffectComponent::intensity)
            .property("useTransform", &ComputeEffectComponent::useTransform)
            .property("localOffset", &ComputeEffectComponent::localOffset)
            .property("radius", &ComputeEffectComponent::radius)
            .property("color", &ComputeEffectComponent::color)
            .property("sizePx", &ComputeEffectComponent::sizePx)
            .property("gravity", &ComputeEffectComponent::gravity)
            .property("drag", &ComputeEffectComponent::drag)
            .property("lifeMin", &ComputeEffectComponent::lifeMin)
            .property("lifeMax", &ComputeEffectComponent::lifeMax)
            .property("depthTest", &ComputeEffectComponent::depthTest)
            .property("depthBiasMeters", &ComputeEffectComponent::depthBiasMeters);

        // === ColliderType enum 등록 ===
        rttr::registration::enumeration<ColliderType>("ColliderType")
            (
                rttr::value("Box", ColliderType::Box),
                rttr::value("Sphere", ColliderType::Sphere),
                rttr::value("Capsule", ColliderType::Capsule)
                );

        rttr::registration::enumeration<MeshColliderType>("MeshColliderType")
            (
                rttr::value("Triangle", MeshColliderType::Triangle),
                rttr::value("Convex", MeshColliderType::Convex)
                );

        // === RigidBodyLockFlags enum 등록 ===
        rttr::registration::enumeration<RigidBodyLockFlags>("RigidBodyLockFlags")
            (
                rttr::value("None", RigidBodyLockFlags::None),
                rttr::value("LockLinearX", RigidBodyLockFlags::LockLinearX),
                rttr::value("LockLinearY", RigidBodyLockFlags::LockLinearY),
                rttr::value("LockLinearZ", RigidBodyLockFlags::LockLinearZ),
                rttr::value("LockAngularX", RigidBodyLockFlags::LockAngularX),
                rttr::value("LockAngularY", RigidBodyLockFlags::LockAngularY),
                rttr::value("LockAngularZ", RigidBodyLockFlags::LockAngularZ)
                );

        // === Phy_RigidBodyComponent 등록 (physicsActorHandle는 내부용이므로 등록하지 않음) ===
        rttr::registration::class_<Phy_RigidBodyComponent>("Phy_RigidBodyComponent")
            .constructor<>()
            .property("density", &Phy_RigidBodyComponent::density)
            .property("massOverride", &Phy_RigidBodyComponent::massOverride)
            .property("isKinematic", &Phy_RigidBodyComponent::isKinematic)
            .property("gravityEnabled", &Phy_RigidBodyComponent::gravityEnabled)
            .property("startAwake", &Phy_RigidBodyComponent::startAwake)
            .property("enableCCD", &Phy_RigidBodyComponent::enableCCD)
            .property("enableSpeculativeCCD", &Phy_RigidBodyComponent::enableSpeculativeCCD)
            .property("lockFlags", &Phy_RigidBodyComponent::lockFlags)
            .property("linearDamping", &Phy_RigidBodyComponent::linearDamping)
            .property("angularDamping", &Phy_RigidBodyComponent::angularDamping)
            .property("maxLinearVelocity", &Phy_RigidBodyComponent::maxLinearVelocity)
            .property("maxAngularVelocity", &Phy_RigidBodyComponent::maxAngularVelocity)
            .property("solverPositionIterations", &Phy_RigidBodyComponent::solverPositionIterations)
            .property("solverVelocityIterations", &Phy_RigidBodyComponent::solverVelocityIterations)
            .property("sleepThreshold", &Phy_RigidBodyComponent::sleepThreshold)
            .property("stabilizationThreshold", &Phy_RigidBodyComponent::stabilizationThreshold)
            .property("teleport", &Phy_RigidBodyComponent::teleport)
            .property("resetVelocityOnTeleport", &Phy_RigidBodyComponent::resetVelocityOnTeleport);

        // === Phy_ColliderComponent 등록 (physicsActorHandle는 내부용이므로 등록하지 않음) ===
        rttr::registration::class_<Phy_ColliderComponent>("Phy_ColliderComponent")
            .constructor<>()
            .property("type", &Phy_ColliderComponent::type)
            .property("halfExtents", &Phy_ColliderComponent::halfExtents)
            .property("radius", &Phy_ColliderComponent::radius)
            .property("capsuleRadius", &Phy_ColliderComponent::capsuleRadius)
            .property("capsuleHalfHeight", &Phy_ColliderComponent::capsuleHalfHeight)
            .property("capsuleAlignYAxis", &Phy_ColliderComponent::capsuleAlignYAxis)
            .property("staticFriction", &Phy_ColliderComponent::staticFriction)
            .property("dynamicFriction", &Phy_ColliderComponent::dynamicFriction)
            .property("restitution", &Phy_ColliderComponent::restitution)
            .property("layerBits", &Phy_ColliderComponent::layerBits)
            .property("ignoreLayers", &Phy_ColliderComponent::ignoreLayers)
            .property("isTrigger", &Phy_ColliderComponent::isTrigger);

        // === Phy_MeshColliderComponent 등록 (physicsActorHandle는 내부용이므로 등록하지 않음) ===
        rttr::registration::class_<Phy_MeshColliderComponent>("Phy_MeshColliderComponent")
            .constructor<>()
            .property("type", &Phy_MeshColliderComponent::type)
            .property("staticFriction", &Phy_MeshColliderComponent::staticFriction)
            .property("dynamicFriction", &Phy_MeshColliderComponent::dynamicFriction)
            .property("restitution", &Phy_MeshColliderComponent::restitution)
            .property("layerBits", &Phy_MeshColliderComponent::layerBits)
            .property("ignoreLayers", &Phy_MeshColliderComponent::ignoreLayers)
            .property("isTrigger", &Phy_MeshColliderComponent::isTrigger)
            .property("meshAssetPath", &Phy_MeshColliderComponent::meshAssetPath)
            .property("flipNormals", &Phy_MeshColliderComponent::flipNormals)
            .property("doubleSidedQueries", &Phy_MeshColliderComponent::doubleSidedQueries)
            .property("validate", &Phy_MeshColliderComponent::validate)
            .property("shiftVertices", &Phy_MeshColliderComponent::shiftVertices)
            .property("vertexLimit", &Phy_MeshColliderComponent::vertexLimit);

        // === Phy_TerrainHeightFieldComponent 등록 (physicsActorHandle는 내부용이므로 등록하지 않음) ===
        rttr::registration::class_<Phy_TerrainHeightFieldComponent>("Phy_TerrainHeightFieldComponent")
            .constructor<>()
            .property("numRows", &Phy_TerrainHeightFieldComponent::numRows)
            .property("numCols", &Phy_TerrainHeightFieldComponent::numCols)
            .property("heightSamples", &Phy_TerrainHeightFieldComponent::heightSamples)
            .property("rowScale", &Phy_TerrainHeightFieldComponent::rowScale)
            .property("colScale", &Phy_TerrainHeightFieldComponent::colScale)
            .property("heightScale", &Phy_TerrainHeightFieldComponent::heightScale)
            .property("centerPivot", &Phy_TerrainHeightFieldComponent::centerPivot)
            .property("doubleSidedQueries", &Phy_TerrainHeightFieldComponent::doubleSidedQueries)
            .property("staticFriction", &Phy_TerrainHeightFieldComponent::staticFriction)
            .property("dynamicFriction", &Phy_TerrainHeightFieldComponent::dynamicFriction)
            .property("restitution", &Phy_TerrainHeightFieldComponent::restitution)
            .property("layerBits", &Phy_TerrainHeightFieldComponent::layerBits)
            .property("ignoreLayers", &Phy_TerrainHeightFieldComponent::ignoreLayers);

        // === EffectComponent 등록 ===
        rttr::registration::class_<EffectComponent>("EffectComponent")
            .constructor<>()
            .property("color", &EffectComponent::color)
            .property("size", &EffectComponent::size)
            .property("enabled", &EffectComponent::enabled)
            .property("alpha", &EffectComponent::alpha);

	// === TrailEffectComponent 등록 (trailSamples는 내부용이므로 등록하지 않음) ===
	rttr::registration::class_<TrailEffectComponent>("TrailEffectComponent")
		.constructor<>()
		.property("color", &TrailEffectComponent::color)
		.property("alpha", &TrailEffectComponent::alpha)
		.property("enabled", &TrailEffectComponent::enabled)
		.property("maxSamples", &TrailEffectComponent::maxSamples)
		.property("sampleInterval", &TrailEffectComponent::sampleInterval)
		.property("fadeDuration", &TrailEffectComponent::fadeDuration);
        // === CCTNonWalkableMode enum 등록 ===
        rttr::registration::enumeration<CCTNonWalkableMode>("CCTNonWalkableMode")
            (
                rttr::value("PreventClimbing", CCTNonWalkableMode::PreventClimbing),
                rttr::value("PreventClimbingAndForceSliding", CCTNonWalkableMode::PreventClimbingAndForceSliding)
            );

        // === CCTCapsuleClimbingMode enum 등록 ===
        rttr::registration::enumeration<CCTCapsuleClimbingMode>("CCTCapsuleClimbingMode")
            (
                rttr::value("Easy", CCTCapsuleClimbingMode::Easy),
                rttr::value("Constrained", CCTCapsuleClimbingMode::Constrained)
            );

        // === Phy_CCTComponent 등록 (내부 핸들과 출력 값들은 제외) ===
        rttr::registration::class_<Phy_CCTComponent>("Phy_CCTComponent")
            .constructor<>()
            .property("radius", &Phy_CCTComponent::radius)
            .property("halfHeight", &Phy_CCTComponent::halfHeight)
            .property("stepOffset", &Phy_CCTComponent::stepOffset)
            .property("contactOffset", &Phy_CCTComponent::contactOffset)
            .property("slopeLimitRadians", &Phy_CCTComponent::slopeLimitRadians)
            .property("nonWalkableMode", &Phy_CCTComponent::nonWalkableMode)
            .property("climbingMode", &Phy_CCTComponent::climbingMode)
            .property("density", &Phy_CCTComponent::density)
            .property("enableQueries", &Phy_CCTComponent::enableQueries)
            .property("layerBits", &Phy_CCTComponent::layerBits)
            .property("ignoreLayers", &Phy_CCTComponent::ignoreLayers)
            .property("hitTriggers", &Phy_CCTComponent::hitTriggers)
            .property("desiredVelocity", &Phy_CCTComponent::desiredVelocity)
            .property("applyGravity", &Phy_CCTComponent::applyGravity)
            .property("gravity", &Phy_CCTComponent::gravity)
            .property("verticalVelocity", &Phy_CCTComponent::verticalVelocity)
            .property("jumpRequested", &Phy_CCTComponent::jumpRequested)
            .property("jumpSpeed", &Phy_CCTComponent::jumpSpeed)
            .property("teleport", &Phy_CCTComponent::teleport);

        // === Phy_SettingsComponent 등록 ===
        rttr::registration::class_<Phy_SettingsComponent>("Phy_SettingsComponent")
            .constructor<>()
            .property("enablePhysics", &Phy_SettingsComponent::enablePhysics)
            .property("enableGroundPlane", &Phy_SettingsComponent::enableGroundPlane)
            .property("groundStaticFriction", &Phy_SettingsComponent::groundStaticFriction)
            .property("groundDynamicFriction", &Phy_SettingsComponent::groundDynamicFriction)
            .property("groundRestitution", &Phy_SettingsComponent::groundRestitution)
            .property("groundLayerBits", &Phy_SettingsComponent::groundLayerBits)
            .property("groundCollideMask", &Phy_SettingsComponent::groundCollideMask)
            .property("groundQueryMask", &Phy_SettingsComponent::groundQueryMask)
            .property("groundIgnoreLayers", &Phy_SettingsComponent::groundIgnoreLayers)
            .property("groundIsTrigger", &Phy_SettingsComponent::groundIsTrigger)
            .property("gravity", &Phy_SettingsComponent::gravity)
            .property("fixedDt", &Phy_SettingsComponent::fixedDt)
            .property("maxSubsteps", &Phy_SettingsComponent::maxSubsteps)
            .property("layerCollideMatrix", &Phy_SettingsComponent::layerCollideMatrix)
            .property("layerQueryMatrix", &Phy_SettingsComponent::layerQueryMatrix)
            .property("layerNames", &Phy_SettingsComponent::layerNames)
            .property("filterRevision", &Phy_SettingsComponent::filterRevision);

        // === Joint enums 등록 ===
        rttr::registration::enumeration<Phy_JointType>("Phy_JointType")
            (
                rttr::value("Fixed", Phy_JointType::Fixed),
                rttr::value("Revolute", Phy_JointType::Revolute),
                rttr::value("Prismatic", Phy_JointType::Prismatic),
                rttr::value("Distance", Phy_JointType::Distance),
                rttr::value("Spherical", Phy_JointType::Spherical),
                rttr::value("D6", Phy_JointType::D6)
            );

        rttr::registration::enumeration<Phy_D6Motion>("Phy_D6Motion")
            (
                rttr::value("Locked", Phy_D6Motion::Locked),
                rttr::value("Limited", Phy_D6Motion::Limited),
                rttr::value("Free", Phy_D6Motion::Free)
            );

        // === Joint 설정 타입 등록 ===
        rttr::registration::class_<Phy_JointFrame>("Phy_JointFrame")
            .constructor<>()
            .property("position", &Phy_JointFrame::position)
            .property("rotation", &Phy_JointFrame::rotation);

        rttr::registration::class_<Phy_RevoluteJointSettings>("Phy_RevoluteJointSettings")
            .constructor<>()
            .property("enableLimit", &Phy_RevoluteJointSettings::enableLimit)
            .property("lowerLimit", &Phy_RevoluteJointSettings::lowerLimit)
            .property("upperLimit", &Phy_RevoluteJointSettings::upperLimit)
            .property("limitStiffness", &Phy_RevoluteJointSettings::limitStiffness)
            .property("limitDamping", &Phy_RevoluteJointSettings::limitDamping)
            .property("limitRestitution", &Phy_RevoluteJointSettings::limitRestitution)
            .property("limitBounceThreshold", &Phy_RevoluteJointSettings::limitBounceThreshold)
            .property("enableDrive", &Phy_RevoluteJointSettings::enableDrive)
            .property("driveVelocity", &Phy_RevoluteJointSettings::driveVelocity)
            .property("driveForceLimit", &Phy_RevoluteJointSettings::driveForceLimit)
            .property("driveFreeSpin", &Phy_RevoluteJointSettings::driveFreeSpin)
            .property("driveLimitsAreForces", &Phy_RevoluteJointSettings::driveLimitsAreForces);

        rttr::registration::class_<Phy_PrismaticJointSettings>("Phy_PrismaticJointSettings")
            .constructor<>()
            .property("enableLimit", &Phy_PrismaticJointSettings::enableLimit)
            .property("lowerLimit", &Phy_PrismaticJointSettings::lowerLimit)
            .property("upperLimit", &Phy_PrismaticJointSettings::upperLimit)
            .property("limitStiffness", &Phy_PrismaticJointSettings::limitStiffness)
            .property("limitDamping", &Phy_PrismaticJointSettings::limitDamping)
            .property("limitRestitution", &Phy_PrismaticJointSettings::limitRestitution)
            .property("limitBounceThreshold", &Phy_PrismaticJointSettings::limitBounceThreshold);

        rttr::registration::class_<Phy_DistanceJointSettings>("Phy_DistanceJointSettings")
            .constructor<>()
            .property("minDistance", &Phy_DistanceJointSettings::minDistance)
            .property("maxDistance", &Phy_DistanceJointSettings::maxDistance)
            .property("tolerance", &Phy_DistanceJointSettings::tolerance)
            .property("enableMinDistance", &Phy_DistanceJointSettings::enableMinDistance)
            .property("enableMaxDistance", &Phy_DistanceJointSettings::enableMaxDistance)
            .property("enableSpring", &Phy_DistanceJointSettings::enableSpring)
            .property("stiffness", &Phy_DistanceJointSettings::stiffness)
            .property("damping", &Phy_DistanceJointSettings::damping);

        rttr::registration::class_<Phy_SphericalJointSettings>("Phy_SphericalJointSettings")
            .constructor<>()
            .property("enableLimit", &Phy_SphericalJointSettings::enableLimit)
            .property("yLimitAngle", &Phy_SphericalJointSettings::yLimitAngle)
            .property("zLimitAngle", &Phy_SphericalJointSettings::zLimitAngle)
            .property("limitStiffness", &Phy_SphericalJointSettings::limitStiffness)
            .property("limitDamping", &Phy_SphericalJointSettings::limitDamping)
            .property("limitRestitution", &Phy_SphericalJointSettings::limitRestitution)
            .property("limitBounceThreshold", &Phy_SphericalJointSettings::limitBounceThreshold);

        rttr::registration::class_<Phy_D6JointDriveSettings>("Phy_D6JointDriveSettings")
            .constructor<>()
            .property("stiffness", &Phy_D6JointDriveSettings::stiffness)
            .property("damping", &Phy_D6JointDriveSettings::damping)
            .property("forceLimit", &Phy_D6JointDriveSettings::forceLimit)
            .property("isAcceleration", &Phy_D6JointDriveSettings::isAcceleration);

        rttr::registration::class_<Phy_D6LinearLimitSettings>("Phy_D6LinearLimitSettings")
            .constructor<>()
            .property("lower", &Phy_D6LinearLimitSettings::lower)
            .property("upper", &Phy_D6LinearLimitSettings::upper)
            .property("stiffness", &Phy_D6LinearLimitSettings::stiffness)
            .property("damping", &Phy_D6LinearLimitSettings::damping)
            .property("restitution", &Phy_D6LinearLimitSettings::restitution)
            .property("bounceThreshold", &Phy_D6LinearLimitSettings::bounceThreshold);

        rttr::registration::class_<Phy_D6TwistLimitSettings>("Phy_D6TwistLimitSettings")
            .constructor<>()
            .property("lower", &Phy_D6TwistLimitSettings::lower)
            .property("upper", &Phy_D6TwistLimitSettings::upper)
            .property("stiffness", &Phy_D6TwistLimitSettings::stiffness)
            .property("damping", &Phy_D6TwistLimitSettings::damping)
            .property("restitution", &Phy_D6TwistLimitSettings::restitution)
            .property("bounceThreshold", &Phy_D6TwistLimitSettings::bounceThreshold);

        rttr::registration::class_<Phy_D6SwingLimitSettings>("Phy_D6SwingLimitSettings")
            .constructor<>()
            .property("yAngle", &Phy_D6SwingLimitSettings::yAngle)
            .property("zAngle", &Phy_D6SwingLimitSettings::zAngle)
            .property("stiffness", &Phy_D6SwingLimitSettings::stiffness)
            .property("damping", &Phy_D6SwingLimitSettings::damping)
            .property("restitution", &Phy_D6SwingLimitSettings::restitution)
            .property("bounceThreshold", &Phy_D6SwingLimitSettings::bounceThreshold);

        rttr::registration::class_<Phy_D6JointSettings>("Phy_D6JointSettings")
            .constructor<>()
            .property("driveLimitsAreForces", &Phy_D6JointSettings::driveLimitsAreForces)
            .property("motionX", &Phy_D6JointSettings::motionX)
            .property("motionY", &Phy_D6JointSettings::motionY)
            .property("motionZ", &Phy_D6JointSettings::motionZ)
            .property("motionTwist", &Phy_D6JointSettings::motionTwist)
            .property("motionSwing1", &Phy_D6JointSettings::motionSwing1)
            .property("motionSwing2", &Phy_D6JointSettings::motionSwing2)
            .property("linearLimitX", &Phy_D6JointSettings::linearLimitX)
            .property("linearLimitY", &Phy_D6JointSettings::linearLimitY)
            .property("linearLimitZ", &Phy_D6JointSettings::linearLimitZ)
            .property("twistLimit", &Phy_D6JointSettings::twistLimit)
            .property("swingLimit", &Phy_D6JointSettings::swingLimit)
            .property("driveX", &Phy_D6JointSettings::driveX)
            .property("driveY", &Phy_D6JointSettings::driveY)
            .property("driveZ", &Phy_D6JointSettings::driveZ)
            .property("driveSwing", &Phy_D6JointSettings::driveSwing)
            .property("driveTwist", &Phy_D6JointSettings::driveTwist)
            .property("driveSlerp", &Phy_D6JointSettings::driveSlerp)
            .property("drivePose", &Phy_D6JointSettings::drivePose)
            .property("driveLinearVelocity", &Phy_D6JointSettings::driveLinearVelocity)
            .property("driveAngularVelocity", &Phy_D6JointSettings::driveAngularVelocity);

        // === Phy_JointComponent 등록 (jointHandle 내부용 제외) ===
        rttr::registration::class_<Phy_JointComponent>("Phy_JointComponent")
            .constructor<>()
            .property("type", &Phy_JointComponent::type)
            .property("targetName", &Phy_JointComponent::targetName)
            .property("frameA", &Phy_JointComponent::frameA)
            .property("frameB", &Phy_JointComponent::frameB)
            .property("collideConnected", &Phy_JointComponent::collideConnected)
            .property("breakForce", &Phy_JointComponent::breakForce)
            .property("breakTorque", &Phy_JointComponent::breakTorque)
            .property("revolute", &Phy_JointComponent::revolute)
            .property("prismatic", &Phy_JointComponent::prismatic)
            .property("distance", &Phy_JointComponent::distance)
            .property("spherical", &Phy_JointComponent::spherical)
            .property("d6", &Phy_JointComponent::d6);

        rttr::registration::class_<IScript>("IScript")
            .constructor<>();

        // === UI 컴포넌트 등록 ===
        // UITransform 등록 (전역 클래스)
        rttr::registration::class_<UITransform>("UITransform")
            .constructor<>()
            .property("m_translation", &UITransform::m_translation)
            .property("m_rotation", &UITransform::m_rotation)
            .property("m_scale", &UITransform::m_scale)
            .property("m_size", &UITransform::m_size)
            .property("m_pivot", &UITransform::m_pivot)
            .property("m_screenSize", &UITransform::m_screenSize);

        // UI_ImageComponent 등록 (public 멤버만)
        // m_path는 GetImagePath/SetImagePath를 통해 property로 등록
        rttr::registration::class_<UI_ImageComponent>("UI_ImageComponent")
            .constructor<>()
            .property("m_size", &UI_ImageComponent::m_size)
            .property("m_srcPos", &UI_ImageComponent::m_srcPos)
            .property("SrcWidthHeight", &UI_ImageComponent::SrcWidthHeight)
            .property("m_pivot", &UI_ImageComponent::m_pivot)
            .property("ImagePath", &UI_ImageComponent::GetImagePath, &UI_ImageComponent::SetImagePath);

        // UI_ScriptComponent 등록
        rttr::registration::class_<UI_ScriptComponent>("UI_ScriptComponent")
            .constructor<>()
            .property("scriptName", &UI_ScriptComponent::scriptName)
            .property("enabled", &UI_ScriptComponent::enabled);
            // instance는 런타임 생성이므로 저장하지 않음

        // IUIScript 등록 (OwnerID만 저장, Owner 포인터는 저장하지 않음)
        rttr::registration::class_<IUIScript>("IUIScript")
            .property("OwnerID", &IUIScript::OwnerID);
    }

    // EditorComponentRegistry에 컴포넌트 등록
    static void RegisterEditorComponentsOnce()
    {
        auto& r = EditorComponentRegistry::Get();

        // Transform은 필수라면 addable/removable 컨트롤
        r.Register<TransformComponent>("Transform", "Core",
            /*addFn*/{}, /*addable*/false, /*removable*/false,
            /*fileKey*/"Transform");

        // Material: 경로 정규화 필요
        r.Register<MaterialComponent>("Material", "Rendering",
            [](World& w, EntityId e) {
                DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
                w.AddComponent<MaterialComponent>(e, defaultColor);
            },
            /*addable*/true, /*removable*/true,
            /*fileKey*/"Material",
            /*serialize*/[](const World& w, EntityId e) -> JsonRttr::json {
                auto* mat = w.GetComponent<MaterialComponent>(e);
                if (!mat) return JsonRttr::json();
                MaterialComponent copy = *mat;
                // MaterialFile::Save에서 이미 정규화하지만, 여기서도 안전하게 처리
                copy.assetPath = SceneSerializationHelpers::NormalizePathToRelative(copy.assetPath);
                copy.albedoTexturePath = SceneSerializationHelpers::NormalizePathToRelative(copy.albedoTexturePath);
                rttr::instance inst = copy;
                return JsonRttr::ToJsonObject(inst);
            },
            /*deserialize*/[](World& w, EntityId e, const JsonRttr::json& j) -> bool {
                auto* mat = w.GetComponent<MaterialComponent>(e);
                if (!mat)
                {
                    DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
                    w.AddComponent<MaterialComponent>(e, defaultColor);
                    mat = w.GetComponent<MaterialComponent>(e);
                }
                if (!mat) return false;
                rttr::instance inst = *mat;
                return JsonRttr::FromJsonObject(inst, j);
            });

        // SkinnedMesh: 로드 시 tmp로 읽고 AddComponent(meshAssetPath) 필요
        r.Register<SkinnedMeshComponent>("Skinned Mesh", "Rendering",
            [](World& w, EntityId e) {
                w.AddComponent<SkinnedMeshComponent>(e, ""); // 기본값
            },
            /*addable*/true, /*removable*/true,
            /*fileKey*/"SkinnedMesh",
            /*serialize*/[](const World& w, EntityId e) -> JsonRttr::json {
                auto* skinned = w.GetComponent<SkinnedMeshComponent>(e);
                if (!skinned) return JsonRttr::json();
                SkinnedMeshComponent copy = *skinned;
                copy.instanceAssetPath = SceneSerializationHelpers::NormalizePathToRelative(copy.instanceAssetPath);
                copy.meshAssetPath = SceneSerializationHelpers::NormalizePathToRelative(copy.meshAssetPath);
                rttr::instance inst = copy;
                return JsonRttr::ToJsonObject(inst);
            },
            /*deserialize*/[](World& w, EntityId e, const JsonRttr::json& j) -> bool {
                SkinnedMeshComponent tmp;
                rttr::instance instTmp = tmp;
                if (!JsonRttr::FromJsonObject(instTmp, j)) return false;
                if (tmp.meshAssetPath.empty())
                    return true;
                SkinnedMeshComponent& sm = w.AddComponent<SkinnedMeshComponent>(e, tmp.meshAssetPath);
                JsonRttr::json j2 = j;
                j2.erase("meshAssetPath");
                rttr::instance inst = sm;
                if (!JsonRttr::FromJsonObject(inst, j2)) return false;
                sm.boneMatrices = &SceneSerializationHelpers::g_IdentityBone;
                sm.boneCount = 1;
                return true;
            });

        r.Register<SkinnedAnimationComponent>("Skinned Animation", "Rendering",
            {}, true, true, /*fileKey*/"SkinnedAnimation");

        r.Register<AdvancedAnimationComponent>("Advanced Animation", "Rendering",
            {}, true, true, /*fileKey*/"AdvancedAnimation");

        r.Register<CameraComponent>("Camera", "Camera",
            {}, true, true, /*fileKey*/"Camera");
        r.Register<CameraFollowComponent>("Camera Follow", "Camera",
            {}, true, true, /*fileKey*/"CameraFollow");
        r.Register<CameraSpringArmComponent>("Spring Arm", "Camera",
            {}, true, true, /*fileKey*/"CameraSpringArm");
        r.Register<CameraLookAtComponent>("Look At", "Camera",
            {}, true, true, /*fileKey*/"CameraLookAt");
        r.Register<CameraShakeComponent>("Shake", "Camera",
            {}, true, true, /*fileKey*/"CameraShake");
        r.Register<CameraBlendComponent>("Blend", "Camera",
            {}, true, true, /*fileKey*/"CameraBlend");
        r.Register<CameraInputComponent>("Input", "Camera",
            {}, true, true, /*fileKey*/"CameraInput");

        r.Register<PointLightComponent>("Point Light", "Lighting",
            {}, true, true, /*fileKey*/"PointLight");
        r.Register<SpotLightComponent>("Spot Light", "Lighting",
            {}, true, true, /*fileKey*/"SpotLight");
        r.Register<RectLightComponent>("Rect Light", "Lighting",
            {}, true, true, /*fileKey*/"RectLight");

        r.Register<ComputeEffectComponent>("Compute Effect", "VFX",
            {}, true, true, /*fileKey*/"ComputeEffect");
        r.Register<EffectComponent>("Effect", "VFX",
            {}, true, true, /*fileKey*/"Effect");
        r.Register<TrailEffectComponent>("Trail Effect", "VFX",
            {}, true, true, /*fileKey*/"TrailEffect");

        r.Register<Phy_RigidBodyComponent>("RigidBody", "Physics",
            {}, true, true, /*fileKey*/"RigidBody");
        r.Register<Phy_ColliderComponent>("Collider", "Physics",
            {}, true, true, /*fileKey*/"Collider");
        r.Register<Phy_MeshColliderComponent>("MeshCollider", "Physics",
            {}, true, true, /*fileKey*/"MeshCollider");
        r.Register<Phy_CCTComponent>("CCT", "Physics",
            {}, true, true, /*fileKey*/"CharacterController");
        r.Register<Phy_TerrainHeightFieldComponent>("TerrainHeightField", "Physics",
            {}, true, true, /*fileKey*/"TerrainHeightField");
        r.Register<Phy_JointComponent>("Joint", "Physics",
            {}, true, true, /*fileKey*/"Joint");
        
        // Phy_SettingsComponent: 수동 직렬화 필요 (중첩 배열)
        r.Register<Phy_SettingsComponent>("Physics Settings", "Physics",
            /*addFn*/{}, /*addable*/true, /*removable*/false,
            /*fileKey*/"PhysicsSceneSettings",
            /*serialize*/[](const World& w, EntityId e) -> JsonRttr::json {
                auto* settings = w.GetComponent<Phy_SettingsComponent>(e);
                if (!settings) return JsonRttr::json();
                return SceneSerializationHelpers::WritePhysicsSceneSettings(*settings);
            },
            /*deserialize*/[](World& w, EntityId e, const JsonRttr::json& j) -> bool {
                auto* settings = w.GetComponent<Phy_SettingsComponent>(e);
                if (!settings)
                {
                    w.AddComponent<Phy_SettingsComponent>(e);
                    settings = w.GetComponent<Phy_SettingsComponent>(e);
                }
                if (!settings) return false;
                return SceneSerializationHelpers::LoadPhysicsSceneSettings(*settings, j);
            });

        r.SortByCategoryThenName();
    }

    // 정적 초기화로 1회 실행
    static const bool s_regEditorComponents = [] {
        RegisterEditorComponentsOnce();
        return true;
    }();
}
