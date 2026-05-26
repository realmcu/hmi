use anyhow::{anyhow, Context, Result};
use rustmcuclaw_common::{NO_LONG_TERM_MEMORY_TEXT, SUMMARY_COMPRESSION_SYSTEM_PROMPT};
use reqwest::Client;
use serde::{Deserialize, Serialize};

use crate::config::{ProviderConfig, ProviderKind};

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum Role {
    System,
    User,
    Assistant,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Message {
    pub role: Role,
    pub content: String,
}

impl Message {
    pub fn system(content: impl Into<String>) -> Self {
        Self {
            role: Role::System,
            content: content.into(),
        }
    }

    pub fn user(content: impl Into<String>) -> Self {
        Self {
            role: Role::User,
            content: content.into(),
        }
    }

    pub fn assistant(content: impl Into<String>) -> Self {
        Self {
            role: Role::Assistant,
            content: content.into(),
        }
    }
}

#[derive(Clone)]
pub struct LlmClient {
    http: Client,
    provider: ProviderConfig,
    api_key: Option<String>,
}

#[derive(Debug, Serialize)]
struct ChatRequest<'a> {
    model: &'a str,
    messages: &'a [Message],
    temperature: f32,
    max_tokens: u32,
}

#[derive(Debug, Deserialize)]
struct ChatResponse {
    choices: Vec<Choice>,
}

#[derive(Debug, Deserialize)]
struct Choice {
    message: ResponseMessage,
}

#[derive(Debug, Deserialize)]
struct ResponseMessage {
    content: String,
}

impl LlmClient {
    pub fn new(provider: ProviderConfig, api_key: Option<String>) -> Self {
        Self {
            http: Client::new(),
            provider,
            api_key,
        }
    }

    pub async fn summarize_dialogue(&self, endpoint: &str, transcript: &str) -> Result<String> {
        if transcript.trim().is_empty() {
            return Ok(String::new());
        }

        if matches!(self.provider.kind, ProviderKind::Mock) {
            return Ok(fallback_summary(transcript));
        }

        let messages = vec![
            Message::system(SUMMARY_COMPRESSION_SYSTEM_PROMPT),
            Message::user(format!("请总结下面这段完整聊天：\n\n{}", transcript.trim())),
        ];

        let summary = self.chat(endpoint, &messages).await?;
        Ok(compact_text(&summary, 50))
    }

    pub async fn chat(&self, endpoint: &str, messages: &[Message]) -> Result<String> {
        if matches!(self.provider.kind, ProviderKind::Mock) {
            let last = messages
                .iter()
                .rev()
                .find(|m| matches!(m.role, Role::User))
                .map(|m| m.content.as_str())
                .unwrap_or("");
            return Ok(format!(
                "[mock] 已收到：{}\n请在配置中选择 open_ai/kimi/github_models/open_ai_compatible 并设置 API Key 以连接云端 LLM。",
                last
            ));
        }

        let key = self
            .api_key
            .as_deref()
            .ok_or_else(|| anyhow!("missing API key; set provider.api_key, or set the environment variable named by provider.api_key_env ({})", self.provider.api_key_env))?;
        if endpoint.trim().is_empty() {
            return Err(anyhow!(
                "provider endpoint is empty; set provider.endpoint in config"
            ));
        }

        let request = ChatRequest {
            model: &self.provider.model,
            messages,
            temperature: self.provider.temperature,
            max_tokens: self.provider.max_tokens,
        };

        let response = self
            .http
            .post(endpoint)
            .bearer_auth(key)
            .json(&request)
            .send()
            .await
            .context("send chat request")?;

        let status = response.status();
        let text = response.text().await.context("read chat response")?;
        if !status.is_success() {
            return Err(anyhow!("LLM request failed {}: {}", status, text));
        }

        let parsed: ChatResponse = serde_json::from_str(&text).context("parse chat response")?;
        parsed
            .choices
            .into_iter()
            .next()
            .map(|c| c.message.content)
            .ok_or_else(|| anyhow!("LLM returned no choices"))
    }
}

fn fallback_summary(transcript: &str) -> String {
    let lines: Vec<&str> = transcript
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty())
        .collect();

    match (lines.first(), lines.last()) {
        (Some(first), Some(last)) if first != last => compact_text(&format!("{}；{}", first, last), 50),
        (Some(first), _) => compact_text(first, 50),
        _ => NO_LONG_TERM_MEMORY_TEXT.to_string(),
    }
}

fn compact_text(text: &str, limit: usize) -> String {
    let compact = text
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty())
        .collect::<Vec<_>>()
        .join(" ");
    let compact = compact
        .trim()
        .trim_matches(|c| matches!(c, '"' | '\'' | '“' | '”'));
    let result: String = compact.chars().take(limit).collect();
    if result.trim().is_empty() {
        NO_LONG_TERM_MEMORY_TEXT.to_string()
    } else {
        result
    }
}
