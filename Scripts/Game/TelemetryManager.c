class TelemetryPlayerRecord
{
	string name;
	string uid;
	string faction;
	ref array<float> coords;
	int status;
}

class TelemetryObjectiveRecord
{
	string name;
	ref array<float> position;
	string factionKey;
	int baseType;
	bool isHQ;
	bool isControlPoint;
	float seizingRadius;
}

class TelemetrySnapshot
{
	string scenarioName;
	string mapName;
	float  updateInterval;
	float  timestamp;
	ref array<ref TelemetryPlayerRecord> players;
	ref array<ref TelemetryObjectiveRecord> objectives;
}

class TelemetryManager
{
	const string PROFILE_CONFIG_PATH = "PlayerTelemetry/config.json";
	// Defaults intentionally left blank so the auto-written config template
	// fails closed — the operator MUST set EndpointHost, ServerTag, and
	// AuthToken in the profile config before the mod will start ticking.
	const string DEFAULT_HOST        = "";
	const string DEFAULT_PATH        = "/api/arma/live-scenario";
	const string DEFAULT_TAG         = "";
	const float  DEFAULT_TICK_SEC    = 5.0;

	private static ref TelemetryManager s_Instance;

	protected ref TelemetryProfileConfig m_RuntimeConfig;
	protected ref TelemetryRestCallback m_Callback;
	protected bool m_bStarted;

	static TelemetryManager GetInstance()
	{
		if (!s_Instance)
			s_Instance = new TelemetryManager();
		return s_Instance;
	}

	void Start()
	{
		if (m_bStarted)
			return;

		if (!Replication.IsServer())
		{
			Print("[Telemetry] Not the server — telemetry disabled.", LogLevel.NORMAL);
			return;
		}

		m_RuntimeConfig = TelemetryProfileConfigLoader.Load(PROFILE_CONFIG_PATH);
		if (!m_RuntimeConfig)
		{
			Print("[Telemetry] Profile config not found at $profile:" +
				  PROFILE_CONFIG_PATH + " — writing default template.",
				  LogLevel.WARNING);
			TelemetryProfileConfigLoader.WriteDefault(
				PROFILE_CONFIG_PATH,
				DEFAULT_HOST,
				DEFAULT_PATH,
				DEFAULT_TAG,
				DEFAULT_TICK_SEC);
			Print("[Telemetry] Fill in EndpointHost, ServerTag, and AuthToken in the new config file and restart the server.", LogLevel.WARNING);
			return;
		}

		if (!m_RuntimeConfig.Enabled)
		{
			Print("[Telemetry] Disabled via profile config.", LogLevel.NORMAL);
			return;
		}

		if (NormalizeHost(m_RuntimeConfig.EndpointHost) == "")
		{
			Print("[Telemetry] EndpointHost is empty in profile config — telemetry disabled until set.", LogLevel.WARNING);
			return;
		}

		if (m_RuntimeConfig.ServerTag == "")
		{
			Print("[Telemetry] ServerTag is empty in profile config — telemetry disabled until set.", LogLevel.WARNING);
			return;
		}

		if (m_RuntimeConfig.AuthToken == "")
		{
			Print("[Telemetry] AuthToken is empty in profile config — telemetry disabled until set.", LogLevel.WARNING);
			return;
		}

		m_Callback = new TelemetryRestCallback();
		m_Callback.m_bDebugLog = m_RuntimeConfig.DebugLog;

		int periodMs = Math.Round(m_RuntimeConfig.TickSeconds * 1000);
		if (periodMs < 1000)
			periodMs = 1000;

		GetGame().GetCallqueue().CallLater(OnTick, periodMs, true);
		m_bStarted = true;

		Print("[Telemetry] Started. Host=" + NormalizeHost(m_RuntimeConfig.EndpointHost) +
			  " Path=" + m_RuntimeConfig.EndpointPath +
			  " Tag=" + m_RuntimeConfig.ServerTag +
			  " EveryMs=" + periodMs, LogLevel.NORMAL);
	}

	void Stop()
	{
		if (!m_bStarted)
			return;
		GetGame().GetCallqueue().Remove(OnTick);
		m_bStarted = false;
		Print("[Telemetry] Stopped.", LogLevel.NORMAL);
	}

	protected void OnTick()
	{
		if (!m_RuntimeConfig || !m_RuntimeConfig.Enabled)
			return;

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		array<int> playerIds = {};
		pm.GetPlayers(playerIds);

		TelemetrySnapshot snapshot = BuildSnapshot(pm, playerIds);

		SCR_JsonSaveContext ctx = new SCR_JsonSaveContext();
		if (!ctx.WriteValue("", snapshot))
		{
			Print("[Telemetry] Snapshot serialization failed.", LogLevel.ERROR);
			return;
		}

		PostSnapshot(ctx.ExportToString());

		if (m_RuntimeConfig.DebugLog)
			Print("[Telemetry] Tick: " + playerIds.Count() + " players queued.", LogLevel.NORMAL);
	}

	protected TelemetrySnapshot BuildSnapshot(PlayerManager pm, array<int> playerIds)
	{
		TelemetrySnapshot snap = new TelemetrySnapshot();
		snap.players = new array<ref TelemetryPlayerRecord>();
		snap.objectives = BuildObjectives();

		snap.scenarioName   = "PlayerTelemetry";
		snap.mapName        = "";
		snap.updateInterval = m_RuntimeConfig.TickSeconds;
		snap.timestamp      = 0.0;
		BaseWorld world = GetGame().GetWorld();
		if (world)
			snap.timestamp = world.GetWorldTime();

		BackendApi backend = GetGame().GetBackendApi();

		foreach (int playerId : playerIds)
		{
			IEntity controlled = pm.GetPlayerControlledEntity(playerId);
			if (!controlled)
				continue;

			TelemetryPlayerRecord rec = new TelemetryPlayerRecord();
			rec.name = pm.GetPlayerName(playerId);
			rec.uid = "";
			if (backend)
				rec.uid = backend.GetPlayerIdentityId(playerId);
			rec.faction = GetFactionKey(controlled);

			vector origin = controlled.GetOrigin();
			rec.coords = new array<float>();
			rec.coords.Insert(origin[0]);
			rec.coords.Insert(origin[1]);
			rec.coords.Insert(origin[2]);

			rec.status = GetStatus(controlled);

			snap.players.Insert(rec);
		}

		return snap;
	}

	protected ref array<ref TelemetryObjectiveRecord> BuildObjectives()
	{
		ref array<ref TelemetryObjectiveRecord> objectives =
			new array<ref TelemetryObjectiveRecord>();

		SCR_MilitaryBaseSystem sys = SCR_MilitaryBaseSystem.GetInstance();
		if (!sys)
			return objectives;

		array<SCR_MilitaryBaseComponent> bases = {};
		sys.GetBases(bases);

		foreach (SCR_MilitaryBaseComponent base : bases)
		{
			if (!base)
				continue;

			SCR_CampaignMilitaryBaseComponent camp =
				SCR_CampaignMilitaryBaseComponent.Cast(base);
			if (!camp)
				continue;

			TelemetryObjectiveRecord rec = new TelemetryObjectiveRecord();

			rec.name = camp.GetBaseName();

			Faction f = camp.GetFaction();
			rec.factionKey = "";
			if (f)
				rec.factionKey = f.GetFactionKey();

			rec.position = new array<float>();
			IEntity owner = base.GetOwner();
			vector origin = vector.Zero;
			if (owner)
				origin = owner.GetOrigin();
			rec.position.Insert(origin[0]);
			rec.position.Insert(origin[1]);
			rec.position.Insert(origin[2]);

			rec.isHQ           = camp.IsHQ();
			rec.isControlPoint = camp.IsControlPoint();

			rec.seizingRadius = 0;
			array<SCR_SeizingComponent> caps = {};
			base.GetCapturePoints(caps);
			if (caps.Count() > 0 && caps[0])
				rec.seizingRadius = caps[0].GetRadius();

			rec.baseType = 0;

			objectives.Insert(rec);
		}

		return objectives;
	}

	protected string GetFactionKey(IEntity controlled)
	{
		SCR_ChimeraCharacter ch = SCR_ChimeraCharacter.Cast(controlled);
		if (!ch)
			return "";
		return ch.GetFactionKey();
	}

	// Priority: DEAD > VEHICLE > ALIVE
	protected int GetStatus(IEntity controlled)
	{
		SCR_ChimeraCharacter ch = SCR_ChimeraCharacter.Cast(controlled);
		if (ch)
		{
			CharacterControllerComponent ctrl = ch.GetCharacterController();
			if (ctrl)
			{
				ECharacterLifeState life = ctrl.GetLifeState();
				if (life == ECharacterLifeState.DEAD)
					return 2;
			}

			CompartmentAccessComponent comp = CompartmentAccessComponent.Cast(
				ch.FindComponent(CompartmentAccessComponent));
			if (comp && comp.IsInCompartment())
				return 3;
		}
		return 0;
	}

	// EndpointHost is documented as a bare hostname (the code forces https://).
	// Operators routinely paste a full URL anyway, e.g. "https://host" or
	// "host/". Left unhandled that produces "https://https://host", which
	// makes every POST fail. Normalize defensively: strip a leading scheme
	// and any trailing slash. Uses only verified string methods
	// (IndexOf/Length/Substring), matching the patterns already in
	// TelemetryProfileConfig.c.
	protected string NormalizeHost(string host)
	{
		int sep = host.IndexOf("://");
		if (sep >= 0)
			host = host.Substring(sep + 3, host.Length() - sep - 3);

		int len = host.Length();
		if (len > 0 && host.Substring(len - 1, 1) == "/")
			host = host.Substring(0, len - 1);

		return host;
	}

	protected void PostSnapshot(string body)
	{
		RestApi api = GetGame().GetRestApi();
		if (!api)
			return;

		string baseUrl = "https://" + NormalizeHost(m_RuntimeConfig.EndpointHost);
		RestContext ctx = api.GetContext(baseUrl);
		if (!ctx)
			return;

		// Enfusion RestContext doesn't reliably transmit custom headers on
		// dedicated servers, so the token rides as a query param. Backend
		// accepts either header or ?token= form.
		string path = m_RuntimeConfig.EndpointPath
			+ "?server=" + m_RuntimeConfig.ServerTag
			+ "&token=" + m_RuntimeConfig.AuthToken;
		ctx.POST(m_Callback, path, body);
	}
}
