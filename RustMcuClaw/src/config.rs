use std::{
    env, fs,
    path::{Path, PathBuf},
};

use anyhow::{Context, Result};
use rustmcuclaw_common::{DEFAULT_HEARTBEAT_INTERVAL_SECS, DEFAULT_HISTORY_LIMIT, DEFAULT_MAX_TOKENS};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Config {
    pub data_dir: PathBuf,
    pub provider: ProviderConfig,
    pub chat: ChatConfig,
    pub heartbeat: HeartbeatConfig,
    pub files: FileConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProviderConfig {
    pub kind: ProviderKind,
    pub model: String,
    pub endpoint: Option<String>,
    pub api_key: Option<String>,
    pub api_key_env: String,
    pub temperature: f32,
    pub max_tokens: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ProviderKind {
    OpenAi,
    Kimi,
    GithubModels,
    GithubCopilotCompatible,
    OpenAiCompatible,
    Mock,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ChatConfig {
    pub history_limit: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HeartbeatConfig {
    pub enabled: bool,
    pub interval_secs: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FileConfig {
    pub soul: PathBuf,
    pub user: PathBuf,
    pub role: PathBuf,
    #[serde(default = "default_summary_memory_file")]
    pub summary_memory: PathBuf,
    pub tasks: PathBuf,
}

impl Default for Config {
    fn default() -> Self {
        let data_dir = default_data_dir();
        Self {
            data_dir: data_dir.clone(),
            provider: ProviderConfig {
                kind: ProviderKind::Mock,
                model: "mock-local".to_string(),
                endpoint: None,
                api_key: None,
                api_key_env: "OPENAI_API_KEY".to_string(),
                temperature: 0.7,
                max_tokens: DEFAULT_MAX_TOKENS,
            },
            chat: ChatConfig {
                history_limit: DEFAULT_HISTORY_LIMIT,
            },
            heartbeat: HeartbeatConfig {
                enabled: true,
                interval_secs: DEFAULT_HEARTBEAT_INTERVAL_SECS,
            },
            files: FileConfig {
                soul: data_dir.join("soul.md"),
                user: data_dir.join("user.md"),
                role: data_dir.join("role.md"),
                summary_memory: data_dir.join("summary_memory.jsonl"),
                tasks: data_dir.join("tasks.json"),
            },
        }
    }
}

impl Config {
    pub fn load_or_default(path: Option<&Path>) -> Result<Self> {
        let path = path.map(Path::to_path_buf).or_else(|| {
            let local = PathBuf::from("config").join("rmcc.toml");
            local.exists().then_some(local)
        });

        match path {
            Some(path) if path.exists() => {
                let raw = fs::read_to_string(&path)
                    .with_context(|| format!("read config {}", path.display()))?;
                let mut cfg: Config = toml::from_str(&raw)
                    .with_context(|| format!("parse config {}", path.display()))?;
                cfg.expand_relative_paths();
                Ok(cfg)
            }
            _ => Ok(Self::default()),
        }
    }

    pub fn ensure_files(&self) -> Result<()> {
        fs::create_dir_all(&self.data_dir)?;
        write_if_missing(
            &self.files.soul,
            "# Soul\n你是 RustMcuClaw，一个内存安全、克制、可靠的终端助手。\n",
        )?;
        write_if_missing(&self.files.user, "# User\n用户喜欢直接、可执行的答案。\n")?;
        write_if_missing(
            &self.files.role,
            "# Role\n保持友好，优先给出可落地步骤。不要编造事实。\n",
        )?;
        write_if_missing(&self.files.summary_memory, "")?;
        write_if_missing(&self.files.tasks, "[]\n")?;
        Ok(())
    }

    pub fn api_key(&self) -> Option<String> {
        if let Some(api_key) = self
            .provider
            .api_key
            .as_ref()
            .map(|value| value.trim())
            .filter(|value| !value.is_empty())
        {
            return Some(api_key.to_string());
        }

        env::var(&self.provider.api_key_env)
            .ok()
            .filter(|s| !s.trim().is_empty())
            .or_else(|| looks_like_api_key(&self.provider.api_key_env).then(|| self.provider.api_key_env.clone()))
    }

    pub fn endpoint(&self) -> String {
        if let Some(endpoint) = &self.provider.endpoint {
            return endpoint.clone();
        }
        match self.provider.kind {
            ProviderKind::OpenAi => "https://api.openai.com/v1/chat/completions".to_string(),
            ProviderKind::Kimi => "https://api.moonshot.cn/v1/chat/completions".to_string(),
            ProviderKind::GithubModels => {
                "https://models.github.ai/inference/chat/completions".to_string()
            }
            ProviderKind::GithubCopilotCompatible
            | ProviderKind::OpenAiCompatible
            | ProviderKind::Mock => String::new(),
        }
    }

    fn expand_relative_paths(&mut self) {
        self.files.soul = absolutize_under(&self.data_dir, &self.files.soul);
        self.files.user = absolutize_under(&self.data_dir, &self.files.user);
        self.files.role = absolutize_under(&self.data_dir, &self.files.role);
        self.files.summary_memory = absolutize_under(&self.data_dir, &self.files.summary_memory);
        self.files.tasks = absolutize_under(&self.data_dir, &self.files.tasks);
    }
}

fn default_summary_memory_file() -> PathBuf {
    PathBuf::from("summary_memory.jsonl")
}

fn default_data_dir() -> PathBuf {
    dirs::data_local_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join("RustMcuClaw")
}

fn absolutize_under(base: &Path, path: &Path) -> PathBuf {
    if path.is_absolute() {
        path.to_path_buf()
    } else {
        base.join(path)
    }
}

fn write_if_missing(path: &Path, content: &str) -> Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    if !path.exists() {
        fs::write(path, content)?;
    }
    Ok(())
}

fn looks_like_api_key(value: &str) -> bool {
    let value = value.trim();
    value.starts_with("sk-")
        || value.starts_with("gsk_")
        || value.starts_with("ghp_")
        || value.starts_with("github_pat_")
}
