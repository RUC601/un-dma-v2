// Prediction orchestration is kept separate from candidate ranking and geometry.
// This file is included inside OW::TargetingDetail.
        inline Vector3 ClampMagnitude(const Vector3& value, float maxLength) {
            const float length = value.get_length();
            if (length <= maxLength) return value;
            return value * (maxLength / length);
        }

        inline Vector3 AccelerationAwareVelocity(const c_entity& entity, float distance, float projectileSpeed) {
            const Motion::EntityMotionEstimate motion = Motion::EstimateEntityMotion(entity);
            const Vector3 baseVelocity = motion.valid ? motion.effectiveVelocity : entity.velocity;
            const uint64_t key = PresentationTiming::ActorControlKey(entity.address ? entity.address : entity.LinkBase,entity.actor_generation);
            if (!key) return baseVelocity;

            const uint64_t now = entity.sample_time_us ? entity.sample_time_us : uint64_t(entity.render_sample_tick_ms)*1000;
            const float safeSpeed = (std::max)(projectileSpeed, 1.0f);
            const float leadTime = std::clamp(distance / safeSpeed, 0.0f, 1.0f);
            Vector3 adjusted = baseVelocity;

            std::lock_guard<std::mutex> lock(velocity_history_mutex);
            if (velocity_history.size() > 256) velocity_history.clear();

            VelocitySample& sample = velocity_history[key];
            if (sample.initialized && now > sample.tick) {
                const float dt = static_cast<float>(now - sample.tick) / 1000000.0f;
                if (dt > 0.001f && dt < 0.5f) {
                    Vector3 acceleration = (baseVelocity - sample.velocity) / dt;
                    acceleration = ClampMagnitude(acceleration, 250.0f);
                    sample.acceleration = acceleration;
                }
            }

            if(!sample.initialized || now>sample.tick) {
                sample.velocity=baseVelocity;sample.tick=now;sample.initialized=true;
            }
            return adjusted + sample.acceleration * std::clamp(leadTime*0.5f,0.0f,0.25f);
        }

        inline ProjectileRuntimeSpec ResolveProjectileRuntimeSpec(const WeaponSpec* weapon,
                                                                  const c_entity& local,
                                                                  bool secondary) {
            const PredictionProfile predictionProfile = ResolvePredictionProfile(weapon);
            const int selectedAlgorithm = ClampPredictionAlgorithm(
                Config::slotTargetRules.active ? Config::slotTargetRules.predictionAlgorithm :
                    predictionProfile.algorithm);
            if (Config::hanzoautospeed &&
                local.HeroID == eHero::HERO_HANJO) {
                Config::predit_level = readult(local.SkillBase + 0x40, 0xB, 0x2A5) * 85.f + 25.f;
                if (local.skill2act) Config::predit_level = 110.f;
            }

            const float fallbackSpeed = Config::predit_level;
            const bool fallbackGravity = Config::projectile_arc ||
                local.HeroID == eHero::HERO_HANJO ||
                Config::Gravitypredit;

            if (!secondary &&
                Config::hanzoautospeed &&
                local.HeroID == eHero::HERO_HANJO &&
                fallbackSpeed > 0.0f) {
                return ProjectileRuntimeSpec{
                    fallbackSpeed,
                    predictionProfile.projectile.gravity,
                    weapon != nullptr,
                    predictionProfile.projectile.launchDirectionUpBias,
                    predictionProfile.projectile.gravityAcceleration,
                    predictionProfile.projectile.castDelayMs,
                    predictionProfile.projectile.projectileRadius,
                    selectedAlgorithm,
                    predictionProfile.projectile.directHitOnly
                };
            }

            if (weapon) {
                return ProjectileRuntimeSpec{
                    predictionProfile.projectile.projectileSpeed,
                    predictionProfile.projectile.gravity,
                    true,
                    predictionProfile.projectile.launchDirectionUpBias,
                    predictionProfile.projectile.gravityAcceleration,
                    predictionProfile.projectile.castDelayMs,
                    predictionProfile.projectile.projectileRadius,
                    selectedAlgorithm,
                    predictionProfile.projectile.directHitOnly
                };
            }

            return ProjectileRuntimeSpec{
                fallbackSpeed,
                fallbackGravity,
                false,
                0.0f,
                9.8f,
                0.0f,
                predictionProfile.projectile.projectileRadius,
                selectedAlgorithm,
                predictionProfile.projectile.directHitOnly
            };
        }

        inline Vector3 SnapshotLocalAimAngleForLeadEstimate() {
            const uint64_t playerControllerBase = SDK ? SDK->g_player_controller.load() : 0;
            if (playerControllerBase) {
                const Vector3 viewDir = ReadPlayerControllerViewDirection(playerControllerBase);
                const float dirLen = viewDir.Size();
                if (std::isfinite(viewDir.X) &&
                    std::isfinite(viewDir.Y) &&
                    std::isfinite(viewDir.Z) &&
                    dirLen > 0.5f &&
                    dirLen < 1.5f) {
                    return DirectionToAimEuler(viewDir);
                }
            }

            Matrix view{}, viewXor{};
            SnapshotViewMatrices(view, viewXor);
            const XMFLOAT3 forward = viewXor.get_rotation();
            const float forwardLen = sqrtf(
                forward.x * forward.x +
                forward.y * forward.y +
                forward.z * forward.z);
            if (std::isfinite(forward.x) &&
                std::isfinite(forward.y) &&
                std::isfinite(forward.z) &&
                forwardLen > 0.5f &&
                forwardLen < 1.5f) {
                return DirectionToAimEuler(Vector3(forward.x, forward.y, forward.z));
            }

            return Vector3{};
        }

        inline Vector3 AimAngleForWorldPoint(const Vector3& worldPoint) {
            const Vector3 camera = CameraPosition();
            const Vector3 delta = worldPoint - camera;
            const float distance = delta.Size();
            if (!std::isfinite(distance) || distance <= 0.0001f)
                return Vector3{};
            return DirectionToAimEuler(delta / distance);
        }

        inline int ResolveLeadAimMethod(bool secondary) {
            if (secondary) {
                return Config::SecondaryAimMethod(Config::Flick2 ? 1 : 0);
            }

            const int behavior = Config::ClampAimBehaviorIndex(Config::View_aimBehavior());
            return Config::AimBehaviorMethod(behavior);
        }

        inline float ResolveLeadSmoothInput(bool secondary) {
            if (secondary) {
                if (Config::Flick2)
                    return Config::AimBehaviorSmoothInput(Config::kAimBehaviorFlick, Config::Flick_smooth2);
                if (Config::Tracking2)
                    return Config::AimBehaviorSmoothInput(Config::kAimBehaviorTracking, Config::Tracking_smooth2);
                return 1.0f;
            }

            const int behavior = Config::ClampAimBehaviorIndex(Config::View_aimBehavior());
            const float scalePercent = Config::IsFlickBehavior(behavior)
                ? Config::View_Flick_smooth()
                : Config::View_Tracking_smooth();
            return Config::AimBehaviorSmoothInput(behavior, scalePercent);
        }

        inline LeadTimingEstimate EstimateLeadTimingForAimPoint(const Vector3& rawAimPoint, bool secondary) {
            const int method = ResolveLeadAimMethod(secondary);
            AimSettleEstimateInput input{};
            input.localAngle = SnapshotLocalAimAngleForLeadEstimate();
            input.targetAngle = AimAngleForWorldPoint(rawAimPoint);
            input.method = method;
            input.slotSpeedScale = ResolveLeadSmoothInput(secondary);
            input.methodSpeedScale = Config::RuntimeAimMethodAngularSpeedScale(method);
            input.constantAngularSpeedDeg = Config::RuntimeAimConstantAngularSpeedDeg();
            input.frameSeconds = kLeadDefaultFrameSeconds;

            return BuildLeadTiming(
                EstimateAimSettleTimeMs(input),
                Config::outputInputDelayMs);
        }

        inline const char* MotionVelocitySourceName(Motion::VelocitySource source) {
            switch (source) {
            case Motion::VelocitySource::Reported:
                return "reported";
            case Motion::VelocitySource::WorldDeltaFallback:
                return "world_delta";
            case Motion::VelocitySource::None:
            default:
                return "none";
            }
        }

        inline LeadPredictionResult ResolveLeadPrediction(const c_entity& entity,
                                                          Vector3 rawAimPoint,
                                                          const ProjectileRuntimeSpec& projectile,
                                                          bool predictionEnabled,
                                                          bool secondary,
                                                          float extraPreFireDelayMs = 0.0f) {
            LeadPredictionResult result{};
            result.predictionEnabled = predictionEnabled;
            result.secondary = secondary;
            result.rawAimPoint = rawAimPoint;
            result.preFireAimPoint = rawAimPoint;
            result.finalAimPoint = rawAimPoint;
            result.projectile = projectile;

            if (!predictionEnabled || IsZeroVector(rawAimPoint))
                return result;

            const Vector3 camera = CameraPosition();
            result.distance = camera.DistTo(rawAimPoint);
            const Motion::EntityMotionEstimate motion = Motion::EstimateEntityMotion(entity);
            const int algorithm = ClampPredictionAlgorithm(projectile.algorithm);
            result.relativeMotion = IsRelativeMotionPredictionAlgorithm(algorithm);
            const c_entity local = SnapshotLocalEntity();
            result.localVelocity = IsFiniteVector(local.velocity) ? local.velocity : Vector3{};
            result.targetVelocity = IsBallisticPredictionAlgorithm(algorithm)
                ? AccelerationAwareVelocity(entity, result.distance, projectile.projectileSpeed)
                : ClampVectorMagnitude(motion.valid ? motion.effectiveVelocity : entity.velocity,
                                       kLeadMaxTargetSpeed);
            result.targetVelocity = ApplyPredictionMotionModel(
                result.targetVelocity, result.localVelocity, algorithm);
            result.timing = EstimateLeadTimingForAimPoint(rawAimPoint, secondary);
            const float preFireMaxMs = (extraPreFireDelayMs > 0.0f || projectile.castDelayMs > 0.0f) ? 1000.0f : kLeadMaxPreFireDelayMs;
            result.timing.preFireDelayMs = ClampLeadDelayMs(
                result.timing.preFireDelayMs + extraPreFireDelayMs + projectile.castDelayMs,
                preFireMaxMs);
            result.preFireAimPoint = IsBallisticPredictionAlgorithm(algorithm)
                ? ApplyTargetMotionPreFireDelay(rawAimPoint, result.targetVelocity,
                                                result.timing.preFireDelayMs, preFireMaxMs)
                : ApplyLegacyLinearPrediction(rawAimPoint, result.targetVelocity,
                                              projectile, result.timing.preFireDelayMs);
            result.finalAimPoint = result.preFireAimPoint;

            if (IsBallisticPredictionAlgorithm(algorithm) &&
                projectile.projectileSpeed > 0.0f) {
                const BallisticIntercept intercept = SolveBallisticIntercept(
                    camera, result.preFireAimPoint, result.targetVelocity, projectile.projectileSpeed,
                    projectile.gravity ? projectile.gravityAcceleration : 0.0f);
                if (intercept.solved)
                    result.finalAimPoint = CompensateProjectileLaunchBias(
                        camera, intercept.aimPoint, projectile.launchDirectionUpBias);
            }

            Diagnostics::Aim("lead.solve algorithm=%s relative=%d enabled=%d secondary=%d projectile(source=%s speed=%.3f gravity=%d acceleration=%.3f upBias=%.4f castMs=%.3f) motion(source=%s confidence=%.3f targetVelocity=(%.3f,%.3f,%.3f) localVelocity=(%.3f,%.3f,%.3f)) timing(settleMs=%.3f inputMs=%d extraPreFireMs=%.3f preFireMs=%.3f) raw=(%.3f,%.3f,%.3f) preFire=(%.3f,%.3f,%.3f) final=(%.3f,%.3f,%.3f)",
                PredictionAlgorithmName(algorithm),
                result.relativeMotion ? 1 : 0,
                predictionEnabled ? 1 : 0,
                secondary ? 1 : 0,
                projectile.fromWeaponSpec ? "weapon" : "legacy",
                projectile.projectileSpeed,
                projectile.gravity ? 1 : 0,
                projectile.gravity ? projectile.gravityAcceleration : 0.0f,
                projectile.launchDirectionUpBias,
                projectile.castDelayMs,
                MotionVelocitySourceName(motion.velocitySource),
                motion.confidence,
                result.targetVelocity.X,
                result.targetVelocity.Y,
                result.targetVelocity.Z,
                result.localVelocity.X,
                result.localVelocity.Y,
                result.localVelocity.Z,
                result.timing.estimatedSettleMs,
                result.timing.inputDelayMs,
                extraPreFireDelayMs,
                result.timing.preFireDelayMs,
                result.rawAimPoint.X,
                result.rawAimPoint.Y,
                result.rawAimPoint.Z,
                result.preFireAimPoint.X,
                result.preFireAimPoint.Y,
                result.preFireAimPoint.Z,
                result.finalAimPoint.X,
                result.finalAimPoint.Y,
                result.finalAimPoint.Z);

            return result;
        }

        inline Vector3 ApplyPrediction(const c_entity& entity, Vector3 position, bool predit, bool secondary) {
            if (IsZeroVector(position)) return position;

            const c_entity local = SnapshotLocalEntity();
            const WeaponSpec* weapon = ResolveWeaponSpec(local.HeroID, Config::View_aimbotAttack());
            const bool resolvedPrediction = ResolvePredictionEnabled(
                ClampPredictionOverride(Config::View_aimbotPredictionMode()),
                weapon,
                predit);
            if (!resolvedPrediction) return position;

            const ProjectileRuntimeSpec projectile = ResolveProjectileRuntimeSpec(weapon, local, secondary);
            return ResolveLeadPrediction(entity, position, projectile, true, secondary).finalAimPoint;
        }
