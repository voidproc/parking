# include <Siv3D.hpp> // OpenSiv3D v0.6.12

namespace
{
	// シーンサイズ関連

	constexpr int32 SceneWidth = 256;
	constexpr int32 SceneHeight = 256;
	constexpr Size SceneSize{ SceneWidth, SceneHeight };
	constexpr Point SceneCenter{ SceneSize.x / 2, SceneSize.y / 2 };
	constexpr Rect SceneRect{ SceneSize };

	// シーンサイズに対するレンダーテクスチャのサイズ（倍率、整数倍）
	constexpr int RenderTextureScale = 2;
}

struct SmokeEffect : IEffect
{
	SmokeEffect(const Vec2& pos, double forwardAngle, double scale = 1.0)
		:
		pos_{ pos + RandomVec2(2.0) },
		forwardAngle_{ forwardAngle + Random(-30_deg, 30_deg) },
		scale_{ scale },
		speed_{ Random(0.08, 0.5 + 1.0 * scale) }
	{
	}

	bool update(double t) override
	{
		const double Lifetime = 0.2 * scale_;
		const double t0_1 = t / Lifetime;

		pos_ += Circular{ speed_, forwardAngle_ + 180_deg };

		Circle{ pos_, (2.0 + 6.0 * t0_1) * scale_ }
			.draw(ColorF{ 1.0, 1.0 - EaseInCubic(t0_1) })
			.drawFrame((3.0 * (1.0 - t0_1)) * scale_, 0.0, ColorF{ 1.0, 1.0 - 0.5 * (t0_1) });

		return t < Lifetime;
	}

	Vec2 pos_;
	double forwardAngle_;
	double scale_;
	double speed_;
};

struct SparkEffect : IEffect
{
	SparkEffect(const Vec2& pos, double speed)
		:
		amp_{ Clamp(EaseOutCubic(speed / 700.0), 0.1, 1.0) },
		pos_{ pos + RandomVec2(Random(0.0, 4.0)) },
		vel_{ Random(1.0, 8.0) * amp_, Math::TwoPi * Random() },
		lifetime_{ (0.3 + Random(-0.1, 0.1)) * amp_ }
	{
	}

	bool update(double t) override
	{
		const double t0_1 = Clamp(t / lifetime_, 0.0, 1.0);
		const auto pos = pos_ + vel_.fastToVec2() * 8.0 * EaseOutCubic(t0_1);
		const Color sparkColor = Sample({ Palette::White, Palette::Red, Palette::Gold });
		RectF{ Arg::center = pos, 0.5 + 6.0 * (1.0 - EaseOutCubic(t0_1)) }.rotated(Math::TwoPi * Random()).draw(sparkColor);

		return t < lifetime_;
	}

	double amp_;
	Vec2 pos_;
	Circular vel_;
	double lifetime_;
};

struct ExplodeEffect : IEffect
{
public:
	ExplodeEffect(const Vec2& pos)
		: pos_{ pos }
	{
	}

	bool update(double t) override
	{
		const double t0_1 = Clamp(t / 0.6, 0.0, 1.0);

		Circle{ pos_, 140.0 * EaseOutCubic(t0_1) }.drawFrame(4.0 - 4.0 * t0_1, 0.0, Palette::Whitesmoke);
		Circle{ pos_, 64.0 * EaseOutCubic(t0_1) }.draw(ColorF{ Palette::Whitesmoke, Periodic::Pulse0_1(0.004s, 0.80 - 0.75 * t0_1)});

		for (int i : step(6 - (int)(t0_1 * 4 * Random())))
		{
			Circle{ pos_ + Circular{ Random(120 * t0_1), Random() * Math::TwoPi }, Random(5.0, 18.0) * (1.0 - 0.5 * t0_1) }.draw(ColorF{ 1.0, Periodic::Square0_1(0.003s) });
		}

		return t < 0.6;
	}

	Vec2 pos_;
};

class Car
{
public:
	Car(P2World& world, Effect& smokeEffect, Effect& sparkEffect, const Vec2& pos, const Color color, double maxSpeed, Circular enemyVelocity = Circular{}, double delay = 0)
		:
		world_{ world },
		smokeEffect_{ smokeEffect },
		sparkEffect_{ sparkEffect },
		color_{ color },
		maxSpeed_{ maxSpeed },
		enemyVelocity_{ enemyVelocity },
		delay_{ delay },
		time_{ StartImmediately::Yes },
		timerSmoke_{ 0.1s, StartImmediately::Yes },
		timerSpark_{ 0.01s, StartImmediately::Yes }
	{
		constexpr P2Material material{ .density = 1.0, .restitution = 0.5, .friction = 0.5, };
		body_ = world.createRect(P2Dynamic, pos, BodySize, material, {});
		body_.setDamping(2.0);
		body_.setAngularDamping(5.0);

		// タイヤ跡（前輪）
		for (int iTire : Range(0, 1))
		{
			trails_ << TrailMotion{}
				.setFrequency(30)
				.setLifeTime(0.2)
				.setPositionFunction([&, iTire](double) { return tirePos_(iTire); })
				.setColorFunction([](double) { return Palette::White; })
				.setAlphaFunction([](double t) { return 0.3 + 0.2 * (1 - t); })
				.setSizeFunction([](double t) { return 0.8 + 0.4 * EaseOutSine(1 - t) + 3.2 * Periodic::Triangle0_1(0.01s); });
		}

		// タイヤ跡（後輪）
		for (int iTire : Range(2, 3))
		{
			trails_ << TrailMotion{}
				.setFrequency(30)
				.setLifeTime(0.3)
				.setPositionFunction([&, iTire](double) { return tirePos_(iTire); })
				.setColorFunction([](double) { return Palette::White; })
				.setAlphaFunction([](double t) { return 0.8 + 0.2 * (1 - t); })
				.setSizeFunction([](double t) { return 0.5 + 0.5 * EaseOutSine(1 - t) + 4.5 * Periodic::Triangle0_1(0.01s); });
		}
	}

	void reset(const Vec2& pos)
	{
		body_.setVelocity(Vec2::Zero());
		body_.setPos(pos);
		body_.setAngularVelocity(0);
		body_.setAngle(0);
	}

	void updateAsEnemy(double stepSec, int enemyType)
	{
		if (life_ <= 0) return;

		if (enemyType == 0)
		{
			body_.setAngle(enemyVelocity_.theta);

			if (time_.sF() > delay_)
			{
				body_.setAngle(enemyVelocity_.theta + 15_deg * Periodic::Sine1_1(3s));
				moveForward(stepSec, enemyVelocity_.r);
			}
		}

		// スピードの限界
		const auto velocity = body_.getVelocity();
		body_.setVelocity(velocity.limitLength(maxSpeed_));

		// 接触をチェック
		checkCollision(stepSec, 60.0);

		// 煙
		generateSmoke(0.8);

		// タイヤ跡
		updateTireTrail(stepSec);
	}

	void updateAsPlayer(double stepSec, bool paused)
	{
		if (life_ <= 0) return;

		if (not paused)
		{
			// 前進
			if (KeyUp.pressed())
			{
				moveForward(stepSec, 8000);
			}

			// 後退
			if (KeyDown.pressed())
			{
				moveBack(stepSec, 8000);
			}

			// ハンドルを左に
			if (KeyLeft.pressed())
			{
				turnLeft(stepSec);
			}

			// ハンドルを右に
			if (KeyRight.pressed())
			{
				turnRight(stepSec);
			}

			// ハンドルが勝手に戻る
			if (not (KeyLeft | KeyRight).pressed())
			{
				freeHandle(stepSec);
			}

			// スピードの限界
			const auto velocity = body_.getVelocity();
			body_.setVelocity(velocity.limitLength(maxSpeed_));
		}

		// 接触をチェック
		checkCollision(stepSec, 12.0);

		// 煙
		generateSmoke();

		// タイヤ跡
		updateTireTrail(stepSec);
	}

	void draw() const
	{
		if (life_ <= 0) return;

		// タイヤ跡
		if (not timerHideTrails_.isRunning())
		{
			for (const auto& t : trails_)
			{
				t.draw();
			}
		}

		// タイヤ

		const Color tireColor = timerCollided_.isRunning() ? Palette::Red.lerp(Palette::White, Periodic::Square0_1(0.08s)) : Palette::Gray.lerp(color_, 0.5);
		const Vec2 posVibCollided = timerCollided_.isRunning() ? RandomVec2(Random(0.5, 2.0)) : Vec2::Zero();
		RectF{ Arg::center = tirePos_(0) + posVibCollided, TireSize }.rotated(angle() + tireAngle_).draw(tireColor);
		RectF{ Arg::center = tirePos_(1) + posVibCollided, TireSize }.rotated(angle() + tireAngle_).draw(tireColor);
		RectF{ Arg::center = tirePos_(2) + posVibCollided, TireSize }.rotated(angle()).draw(tireColor);
		RectF{ Arg::center = tirePos_(3) + posVibCollided, TireSize }.rotated(angle()).draw(tireColor);

		// 本体

		const Vec2 bodyPosVib = Circular{ 1.0 * Periodic::Sine1_1(0.08s), angle() } + RandomVec2(0.5);
		const Color bodyColor = timerCollided_.isRunning() ? Palette::Red.lerp(Palette::White, 0.5 + 0.5 * Periodic::Square0_1(0.08s)) : color_;
		const Color damagedBodyColor = life_ >= 70.0 ? bodyColor : bodyColor.lerp(Palette::Red, Periodic::Pulse0_1(SecondsF{ 0.05 + 0.3 * (life_ / 100.0) }, 0.08 + 0.2 * (1.0 - life_ / 100.0)));
		bodyQuad().movedBy(bodyPosVib + posVibCollided).draw(damagedBodyColor);
	}

	Quad bodyQuad() const
	{
		return RectF{ Arg::center = pos(), BodySize }.rotated(angle());
	}

	Vec2 pos() const
	{
		return body_.getPos();
	}

	double angle() const
	{
		return body_.getAngle();
	}

	void releaseBody()
	{
		body_.release();
		alive_ = false;
	}

	void hideTrails()
	{
		timerHideTrails_.restart(1s);
	}

	void resetLife()
	{
		life_ = 100.0;
	}

	double life() const
	{
		return life_;
	}

	bool alive() const
	{
		return alive_;
	}

private:
	void moveForward(double stepSec, double force)
	{
		const auto forwardVec = Circular{ force, angle() + tireAngle_ }.fastToVec2();
		body_.applyForceAt(forwardVec * stepSec, pos() + Circular{ 8.0, angle() });
		body_.setAngularVelocity(tireAngle_ * 3.0);
	}

	void moveBack(double stepSec, double force)
	{
		const auto forwardVec = Circular{ force, angle() + tireAngle_ }.fastToVec2();
		body_.applyForceAt(-forwardVec * 0.8 * stepSec, pos() + Circular{ 8.0, angle() });
		body_.setAngularVelocity(-tireAngle_ * 3.0);
	}

	void turnLeft(double stepSec)
	{
		tireAngle_ = Clamp(tireAngle_ - 150_deg * stepSec, -45_deg, 45_deg);
	}

	void turnRight(double stepSec)
	{
		tireAngle_ = Clamp(tireAngle_ + 150_deg * stepSec, -45_deg, 45_deg);
	}

	void freeHandle(double stepSec)
	{
		tireAngle_ = Math::Lerp(tireAngle_, 0, 10.0 * stepSec);
	}

	void checkCollision(double stepSec, double damage)
	{
		for (auto&& [pair, collision] : world_.getCollisions())
		{
			for (const auto& contact : collision)
			{
				const auto velocity = body_.getVelocity();

				if (timerSpark_.reachedZero() && velocity.length() > 4.0)
				{
					timerSpark_.restart();

					for (int i : step(Random(1, 2)))
					{
						sparkEffect_.add<SparkEffect>(contact.point, velocity.length());
					}
				}
			}

			if (not timerCollided_.isRunning() && (pair.a == body_.id() || pair.b == body_.id()))
			{
				timerCollided_.restart(0.3s);
			}
		}

		// 接触ダメージ
		if (timerCollided_.isRunning())
		{
			if (life_ > 0)
			{
				life_ -= damage * stepSec;

				if (life_ <= 0)
				{
					// 爆発エフェクト
					sparkEffect_.add<ExplodeEffect>(body_.getPos());
				}
			}
		}
	}

	void generateSmoke(double scale = 1.0)
	{
		if (timerSmoke_.reachedZero())
		{
			timerSmoke_.restart(SecondsF{ Random(0.001, 0.1) });

			for (int i : step(Random(1, 3)))
			{
				smokeEffect_.add<SmokeEffect>(pos() + Circular{ 12.0, angle() + 180_deg }, angle() + tireAngle_, scale);
			}

			if (body_.getVelocity().length() > 1.0)
			{
				for (int iTire : step(4))
				{
					smokeEffect_.add<SmokeEffect>(tirePos_(iTire) + RandomVec2(2.0), angle() + tireAngle_ * 0.3, 0.3 * scale);
				}
			}
		}
	}

	void updateTireTrail(double stepSec)
	{
		for (auto& t : trails_)
		{
			t.update(stepSec);
		}
	}

	// タイヤの位置
	// index: 0-3 (時計回り)
	Vec2 tirePos_(int index) const
	{
		switch (index)
		{
		case 0: return pos() + Circular{ 12, angle() - 35_deg };
		case 1: return pos() + Circular{ 12, angle() + 35_deg };
		case 2: return pos() + Circular{ 12, angle() + 145_deg };
		case 3: return pos() + Circular{ 12, angle() + 215_deg };
		}
		return pos();
	}

private:
	P2World& world_;
	Effect& smokeEffect_;
	Effect& sparkEffect_;
	Color color_;
	double maxSpeed_;
	Circular enemyVelocity_;
	double delay_ = 0;
	Stopwatch time_;
	P2Body body_;

	// タイヤの向き
	double tireAngle_ = 0;

	// エフェクト用タイマー
	Timer timerSmoke_;
	Timer timerSpark_;
	Timer timerCollided_;

	// タイヤの跡
	Array<TrailMotion> trails_;
	Timer timerHideTrails_;

	// 耐久力
	double life_ = 100;
	bool alive_ = true;

	static inline constexpr SizeF BodySize{ 16, 28 };
	static inline constexpr SizeF TireSize{ 6, 8 };
};

struct Wall
{
	P2Body body;
	RectF rect;
};

struct Goal
{
	RectF area;
	Color color;
	static inline constexpr SizeF Size{ 48, 64 };
};

void RemoveEnemies(Array<Car>& enemies)
{
	for (auto& e : enemies)
	{
		e.releaseBody();
	}
	enemies.clear();
}

void RemoveWalls(Array<Wall>& walls)
{
	for (auto& w : walls)
	{
		w.body.release();
	}
	walls.clear();
}

void AddWall(P2World& world, Array<Wall>& walls, const RectF& rect)
{
	walls << Wall{ world.createRect(P2Static, rect.center(), rect.size, {}, {}), rect };
}

void LoadStage(int stage, P2World& world, Array<Wall>& walls, Array<Car>& enemies, Car& player, Goal& goal, Effect& smokeEffect, Effect& sparkEffect)
{
	RemoveEnemies(enemies);
	RemoveWalls(walls);

	player.hideTrails();
	player.resetLife();

	if (stage == 1)
	{
		player.reset(Vec2{ 128, 128 });

		goal.area = RectF{ Arg::center = Vec2{ 400, 128 }, Goal::Size };

		AddWall(world, walls, RectF{ Arg::center = Vec2{ 128, 16 }, 40000, 8 });
		AddWall(world, walls, RectF{ Arg::center = Vec2{ 128, 256 - 16 }, 40000, 8 });
		AddWall(world, walls, RectF{ Arg::center = Vec2{ -250, 128 }, 8, 256 - 16 });

		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ -160, 128 }, Palette::Tomato, 900, Circular{ 1500, 90_deg });
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ -120, 128 }, Palette::Tomato, 900, Circular{ 1500, 90_deg });
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ -80, 128 }, Palette::Tomato, 900, Circular{ 1500, 90_deg });
	}
	else if (stage == 2)
	{
		player.reset(Vec2{ 1150, 632 });

		goal.area = RectF{ 999, 601, Goal::Size };

		AddWall(world, walls, RectF{ 1072, 465, 8, 904 });
		AddWall(world, walls, RectF{ 1244, 351, 360, 348 });
		AddWall(world, walls, RectF{ 1053, 805, 741, 8 });
		AddWall(world, walls, RectF{ 1774, 516, 8, 310 });
		AddWall(world, walls, RectF{ 80, 366, 1976, 8 });
		AddWall(world, walls, RectF{ 252, 1739, 534, 8 });
		AddWall(world, walls, RectF{ 766, 351, 8, 1404 });
		AddWall(world, walls, RectF{ 909, 471, 8, 1458 });
		AddWall(world, walls, RectF{ 67, 1910, 877, 8 });
		AddWall(world, walls, RectF{ 84, 1552, 8, 381 });
		AddWall(world, walls, RectF{ 906, 466, 183, 8 });
		AddWall(world, walls, RectF{ 67, 1571, 534, 8 });
		AddWall(world, walls, RectF{ 1051, 1021, 754, 8 });
		AddWall(world, walls, RectF{ 2032, 348, 8, 1211 });
		AddWall(world, walls, RectF{ 583, 962, 8, 644 });
		AddWall(world, walls, RectF{ 560, 974, 231, 8 });
		AddWall(world, walls, RectF{ 890, 1526, 1158, 8 });
		AddWall(world, walls, RectF{ 1774, 1002, 8, 393 });
		AddWall(world, walls, RectF{ 1244, 1196, 360, 348 });
	}
	else if (stage == 3)
	{
		player.reset(Vec2{ 1100, 616 });

		goal.area = RectF{ 1073, 425, Goal::Size.yx() };

		AddWall(world, walls, RectF{ 255, 510, 1949, 64 });
		AddWall(world, walls, RectF{ 494, 760, 1995, 61 });
		AddWall(world, walls, RectF{ -13, 8, 2839, 319 });
		AddWall(world, walls, RectF{ 968, 236, 61, 389 });
		AddWall(world, walls, RectF{ 2144, 453, 61, 318 });
		AddWall(world, walls, RectF{ 243, 1026, 1938, 61 });
		AddWall(world, walls, RectF{ 2428, 798, 61, 550 });
		AddWall(world, walls, RectF{ 242, 510, 61, 841 });
		AddWall(world, walls, RectF{ 678, 942, 137, 136 });
		AddWall(world, walls, RectF{ 1281, 938, 137, 136 });
		AddWall(world, walls, RectF{ 993, 1041, 137, 136 });
		AddWall(world, walls, RectF{ 1721, 1044, 137, 136 });
		AddWall(world, walls, RectF{ 541, 1287, 1938, 61 });
		AddWall(world, walls, RectF{ -13, 1547, 2839, 270 });
		AddWall(world, walls, RectF{ 2765, 22, 61, 1782 });
		AddWall(world, walls, RectF{ -12, 22, 61, 1782 });
		AddWall(world, walls, RectF{ 1000, 1469, 137, 136 });
		AddWall(world, walls, RectF{ 1459, 1302, 137, 136 });
		AddWall(world, walls, RectF{ 1941, 1454, 137, 136 });
		AddWall(world, walls, RectF{ 2347, 400, 281, 231 });
		AddWall(world, walls, RectF{ 2620, 1405, 182, 201 });
		AddWall(world, walls, RectF{ 2100, 986, 137, 136 });
		AddWall(world, walls, RectF{ 1319, 437, 271, 123 });
		AddWall(world, walls, RectF{ 1737, 262, 271, 136 });

		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 2060, 660 }, Palette::Tomato, 900, Circular{ 3000, -90_deg });
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 1860, 660 }, Palette::Tomato, 900, Circular{ 3000, -90_deg });
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 1660, 660 }, Palette::Tomato, 900, Circular{ 3000, -90_deg });

		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 2358, 878 }, Palette::Tomato, 900, Circular{ 3500, -90_deg }, 5.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 2158, 888 }, Palette::Tomato, 900, Circular{ 3500, -90_deg }, 5.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 1958, 848 }, Palette::Tomato, 900, Circular{ 3500, -90_deg }, 5.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 1758, 878 }, Palette::Tomato, 900, Circular{ 3500, -90_deg }, 5.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 1558, 858 }, Palette::Tomato, 900, Circular{ 3500, -90_deg }, 5.0);

		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 364, 1238 }, Palette::Tomato, 900, Circular{ 4000, 90_deg }, 14.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 564, 1228 }, Palette::Tomato, 900, Circular{ 4000, 90_deg }, 13.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 764, 1248 }, Palette::Tomato, 900, Circular{ 4000, 90_deg }, 12.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 964, 1238 }, Palette::Tomato, 900, Circular{ 4000, 90_deg }, 11.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 1164, 1228 }, Palette::Tomato, 900, Circular{ 4000, 90_deg }, 10.0);

		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 130, 417 }, Palette::Tomato, 900, Circular{ 5000, 180_deg }, 25.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 95, 407 }, Palette::Tomato, 900, Circular{ 5000, 180_deg }, 25.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 155, 407 }, Palette::Tomato, 900, Circular{ 5000, 180_deg }, 25.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 120, 617 }, Palette::Tomato, 900, Circular{ 5100, 180_deg }, 22.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 95, 607 }, Palette::Tomato, 900, Circular{ 5100, 180_deg }, 22.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 165, 607 }, Palette::Tomato, 900, Circular{ 5100, 180_deg }, 22.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 130, 817 }, Palette::Tomato, 900, Circular{ 5200, 180_deg }, 20.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 95, 807 }, Palette::Tomato, 900, Circular{ 5200, 180_deg }, 20.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 155, 807 }, Palette::Tomato, 900, Circular{ 5200, 180_deg }, 20.0);

		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 2619, 729 }, Palette::Tomato, 900, Circular{ 6400, 180_deg }, 33.5 - 2.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 2649, 729 }, Palette::Tomato, 900, Circular{ 6400, 180_deg }, 34.0 - 2.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 2589, 729 }, Palette::Tomato, 900, Circular{ 6400, 180_deg }, 34.5 - 2.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 2619, 829 }, Palette::Tomato, 900, Circular{ 6400, 180_deg }, 29.5 - 2.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 2649, 829 }, Palette::Tomato, 900, Circular{ 6400, 180_deg }, 30.0 - 2.0);
		enemies.emplace_back(world, smokeEffect, sparkEffect, Vec2{ 2589, 829 }, Palette::Tomato, 900, Circular{ 6400, 180_deg }, 30.5-2.0);

	}
	else if (stage == 0)
	{
		player.reset(Vec2{ 128, 128 });

		goal.area = RectF{};
	}
}

void Main()
{
	Scene::SetBackground(ColorF{ 0 });

	Window::SetTitle(U"PARKING v1.0.0");

	// ESCキーで終了しない
	System::SetTerminationTriggers(UserAction::CloseButtonClicked);

	// 低解像度のシーン
	Scene::Resize(SceneSize * RenderTextureScale);
	Scene::SetTextureFilter(TextureFilter::Nearest);
	const ScopedRenderStates2D renderState{ SamplerState::ClampNearest };
	RenderTexture renderTexture(SceneSize);

	// ウィンドウサイズを config.ini から読み込み
	INI ini{ U"config.ini" };
	const double scale = ini.getOr<double>(U"WindowScale", 2.0);
	Window::Resize((SceneSize * scale).asPoint());

	// アセット
	FontAsset::Register(U"Title", 12, Resource(U"font/x8y12pxTheStrongGamer.ttf"), FontStyle::Bitmap);

	// 2D 物理演算のシミュレーション
	constexpr double StepSec = 1.0 / 200.0;
	double accumulatorSec = 0.0;

	// 2D 物理演算のワールド
	P2World world{ 0.0 };

	// ゴール
	Goal goal;

	// 壁
	Array<Wall> walls;
	//Array<P2Body> walls;
	walls.reserve(100);

	// 各種エフェクト
	Effect smokeEffect, sparkEffect;

	// プレイヤー
	Car player{ world, smokeEffect, sparkEffect, Vec2{ 128, 128 }, Palette::White, 700 };

	// 敵
	Array<Car> enemies;
	enemies.reserve(100);

	// 2D カメラ
	double zoom = 1.0;
	auto cameraParam = Camera2DParameters::NoControl();
	cameraParam.positionSmoothTime = 0.05;
	Camera2D camera{ player.pos(), 1.0, cameraParam };

	// 地面のテクスチャ
	const auto groundImage = Image{ Resource(U"example/texture/ground.jpg") }.grayscale().threshold(100);
	const Texture groundTexture{ groundImage };

	// 地面の色
	std::array<Color, 4> groundColor = {
		Palette::Darkkhaki.lerp(Palette::Black, 0.5),
		Palette::Darkslategray.lerp(Palette::Black, 0.5),
		Palette::Darkgreen.lerp(Palette::Black, 0.5),
		Palette::Darkred.lerp(Palette::Black, 0.5),
	};

	// シーン進行管理
	Stopwatch timeTitle{ StartImmediately::Yes };
	Stopwatch timeGame;
	Stopwatch timeStage;
	int stage = 0;
	const int StageCount = 3;
	Stopwatch timeJudgeParking;
	Stopwatch timeShowRecord;
	Stopwatch timeGameover;

	// タイトルに戻る？メニュー
	Stopwatch timeShowMenu;
	int menuCursor = 0;

	// 記録
	Optional<int32> record;
	
	while (System::Update())
	{
		// タイトルシーン
		if (timeTitle.isRunning())
		{
			if (KeyEnter.down())
			{
				// メインのシーンに移行
				stage = 1;
				LoadStage(stage, world, walls, enemies, player, goal, smokeEffect, sparkEffect);

				timeTitle.reset();
				timeGame.start();
				timeStage.start();
				continue;
			}
		}
		else
		{
			// ESC キーでタイトルに戻るダイアログ
			if (KeyEscape.down())
			{
				if (timeShowMenu.isRunning())
				{
					timeShowMenu.reset();
				}
				else
				{
					timeShowMenu.restart();
					menuCursor = 0;
				}
			}

			// タイトルに戻るダイアログの操作
			if (timeShowMenu.isRunning())
			{
				if ((KeyLeft | KeyRight | KeyUp | KeyDown).down())
				{
					menuCursor = (menuCursor + 1) % 2;
				}

				if (KeyEnter.down())
				{
					if (menuCursor == 0)
					{
						// メニューを閉じる
						timeShowMenu.reset();
					}
					else
					{
						// タイトルへ
						stage = 0;
						LoadStage(0, world, walls, enemies, player, goal, smokeEffect, sparkEffect);

						timeShowMenu.reset();
						timeGame.reset();
						timeStage.reset();
						timeShowRecord.reset();
						timeTitle.restart();
						continue;
					}
				}
			}

			// スペースキーでカメラズームアウト
			if (KeySpace.pressed())
			{
				zoom = Clamp(zoom - 2.0 * Scene::DeltaTime(), 0.65, 1.0);
			}
			else
			{
				zoom = Clamp(zoom + 6.0 * Scene::DeltaTime(), 0.65, 1.0);
			}

			camera.setScale(zoom);
		}

		// メインシーン

		const bool isInGoal = goal.area.contains(player.bodyQuad());

		if (timeStage.isRunning())
		{
			// ゴールに完全に入ったかの判定…

			if (not timeJudgeParking.isRunning() && isInGoal)
			{
				timeJudgeParking.restart();
			}

			if (timeJudgeParking.isRunning())
			{
				if (not isInGoal)
				{
					timeJudgeParking.reset();
				}
				else if (timeJudgeParking > 1s && not timeGameover.isRunning())
				{
					// クリアしたのでクリアタイム表示へ移行
					timeJudgeParking.reset();
					timeStage.pause();
					timeShowRecord.restart();
				}
			}

			// プレイヤーが壊れている？
			if (not timeGameover.isRunning() && player.life() <= 0)
			{
				timeGameover.restart();
			}

			if (timeGameover > 5s)
			{
				// タイトルへ
				stage = 0;
				LoadStage(0, world, walls, enemies, player, goal, smokeEffect, sparkEffect);

				timeGameover.reset();
				timeGame.reset();
				timeStage.reset();
				timeShowRecord.reset();
				timeTitle.restart();
				timeShowMenu.reset();
				continue;
			}
		}

		// ステージのクリアタイムを表示し、その後次のステージへ移行
		if (timeShowRecord.isRunning())
		{
			if (timeShowRecord > 3s)
			{
				// 次のステージへ
				if (stage < StageCount)
				{
					stage += 1;
					LoadStage(stage, world, walls, enemies, player, goal, smokeEffect, sparkEffect);

					timeStage.restart();
					timeShowRecord.reset();
					continue;
				}

				// 全てのステージをクリアしたのでタイトルへ
				if (stage == StageCount)
				{
					if (not record || timeGame.ms() < *record)
					{
						record = timeGame.ms();
					}

					stage = 0;
					LoadStage(0, world, walls, enemies, player, goal, smokeEffect, sparkEffect);

					timeGame.reset();
					timeStage.reset();
					timeShowRecord.reset();
					timeTitle.restart();
					continue;
				}
			}
		}

		// 2D 物理演算のワールドを更新
		for (accumulatorSec += Scene::DeltaTime(); (StepSec <= accumulatorSec); accumulatorSec -= StepSec)
		{
			player.updateAsPlayer(StepSec, timeShowMenu.isRunning());

			for (auto& e : enemies)
			{
				e.updateAsEnemy(StepSec, 0);
			}

			world.update(StepSec);
		}

		for (auto& e : enemies)
		{
			if (e.alive() && e.life() <= 0)
			{
				e.releaseBody();
			}
		}

		// カメラをプレイヤーに追従
		camera.setTargetCenter(player.pos());
		camera.update();

		// 描画
		{
			const ScopedRenderTarget2D renderTarget{ renderTexture };

			// 背景全体の色
			Scene::Rect().draw(groundColor[stage]);

			// タイトルシーン
			if (timeTitle.isRunning())
			{
				FontAsset(U"Title")(U"PARKING").drawAt(24, SceneCenter.movedBy(0, -36), ColorF{1.0, 0.5});
				FontAsset(U"Title")(U"PRESS ENTER").drawAt(12, SceneCenter.movedBy(0, 36), ColorF{ 1.0, 0.5 });

				if (record)
				{
					FontAsset(U"Title")(U"BEST REC. {:02d}:{:02d}.{:02d}"_fmt(*record / 1000 / 60, (*record / 1000) % 60, (*record % 1000) / 10))
						.drawAt(12, SceneCenter.movedBy(0, 110), ColorF{ 1.0, 0.5 });
				}
			}

			{
				// 2D カメラ
				const auto cameraTr = camera.createTransformer();

				// 共通
				{
					//プレイヤーの角度に追従した回転
					const Transformer2D rotTr(Mat3x2::Rotate(-player.angle(), player.pos()));

					// ゴール
					goal.area
						.draw(ColorF{ 1.0, 0.1 + 0.1 * Periodic::Jump1_1(0.1s) })
						.drawFrame(4, 0, ColorF{ isInGoal ? Palette::Lime : Palette::White, 0.75 + 0.25 * Periodic::Jump1_1(0.2s) });

					// 地面
					{
						const ScopedRenderStates2D sampler{ SamplerState::RepeatNearest };

						groundTexture.mapped(40000, 40000).draw(Arg::center = Vec2::Zero(), AlphaF(0.1));
					}

					// 壁
					for (const auto& wall : walls)
					{
						wall.rect.draw(Palette::Whitesmoke);
					}

					// 煙
					smokeEffect.update();

					// プレイヤー
					player.draw();

					// 敵
					for (const auto& e : enemies)
					{
						e.draw();
					}

					// スパーク
					sparkEffect.update();
				}
			}

			// ゲームシーン
			if (timeStage.isRunning())
			{
				// タイム
				const auto textTime = FontAsset(U"Title")(U"{:02d}:{:02d}.{:02d}"_fmt(timeStage.min(), timeStage.s() % 60, (timeStage.ms() % 1000) / 10));
				textTime.drawAt(12, SceneCenter.movedBy(1, -118 + 1), ColorF{ 0, 0.5 });
				textTime.drawAt(12, SceneCenter.movedBy(0, -118), ColorF{ 1.0 });

				// ステージ名
				if (timeStage < 3s)
				{
					RectF{ Arg::center = SceneCenter.movedBy(0, 110 + 2), 256, 20 }.draw(Palette::Black);
					const auto text = FontAsset(U"Title")(U"STAGE {}"_fmt(stage));
					text.drawAt(12, SceneCenter.movedBy(1, 110 + 1), ColorF{ 0, 0.5 });
					text.drawAt(12, SceneCenter.movedBy(0, 110), ColorF{ 1.0 });
				}
			}

			// ステージのクリアタイム表示
			if (timeShowRecord.isRunning())
			{
				const auto textRec = FontAsset(U"Title")(U"RECORD {:02d}:{:02d}.{:02d}"_fmt(timeStage.min(), timeStage.s() % 60, (timeStage.ms() % 1000) / 10));
				const double alpha = timeShowRecord < 1s ? Periodic::Square0_1(0.2s) : 1.0;
				textRec.drawAt(12, SceneCenter.movedBy(1, -48 + 1), ColorF{ 0, 0.5 * alpha });
				textRec.drawAt(12, SceneCenter.movedBy(0, -48), ColorF{ 1.0, alpha });
			}

			// タイトルに戻る？メニュー
			if (timeShowMenu.isRunning())
			{
				RectF{ Arg::center = SceneCenter, 256, 256 }.draw(ColorF{ 0, 0.8 });
				FontAsset(U"Title")(U"RETURN TO TITLE?").drawAt(12, SceneCenter.movedBy(0, -48), ColorF{ 1.0 });

				RectF{ Arg::center = SceneCenter.movedBy(0, 30 + 18 * menuCursor + 2), 256, 14 }.draw(ColorF{ Palette::Blue, 0.8 * Periodic::Jump0_1(0.3s) });

				FontAsset(U"Title")(U"CANCEL").drawAt(12, SceneCenter.movedBy(0, 30), ColorF{ 0.7 + 0.3 * (menuCursor == 0) });
				FontAsset(U"Title")(U"OK (TO TITLE)").drawAt(12, SceneCenter.movedBy(0, 48), ColorF{ 0.7 + 0.3 * (menuCursor == 1) });
			}

			// ゲームオーバー
			if (timeGameover.isRunning())
			{
				RectF{ Arg::center = SceneCenter, 256, 256 }.draw(ColorF{ Palette::Darkred, 0.3 });
				const auto text = FontAsset(U"Title")(U"GAME OVER");
				text.drawAt(24, SceneCenter.movedBy(2, 2), ColorF{ 0, 0.5 });
				text.drawAt(24, SceneCenter.movedBy(0, 0), ColorF{ 1.0 });
			}
		}

		{
			const Transformer2D scaler{ Mat3x2::Scale(scale) };
			renderTexture.draw();
		}

		Circle{ Scene::CenterF(), Scene::Width() * Math::Sqrt2 / 2 }.draw(ColorF{ 0, 0 }, ColorF{ 0, 0.2 });
	}
}
