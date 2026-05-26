mod app;
mod config;
mod llm;
mod memory;
mod profiles;
mod tasks;

use std::path::PathBuf;

use anyhow::Result;
use clap::{Parser, Subcommand};

use crate::app::AssistantApp;
use crate::config::Config;

#[derive(Debug, Parser)]
#[command(name = "rustmcuclaw", version, about = "OpenClaw-like Rust terminal assistant")]
struct Cli {
    /// Path to config file. If omitted, ./config/rmcc.toml is used when present.
    #[arg(short, long)]
    config: Option<PathBuf>,

    #[command(subcommand)]
    command: Option<Command>,
}

#[derive(Debug, Subcommand)]
enum Command {
    /// Start interactive terminal chat.
    Chat,
    /// Send one prompt and print one answer.
    Ask { prompt: String },
    /// Add/list/complete periodic tasks.
    Task {
        #[command(subcommand)]
        command: TaskCommand,
    },
    /// Print current configuration and data paths.
    Doctor,
    /// Create default config/profile files if missing.
    Init,
}

#[derive(Debug, Subcommand)]
enum TaskCommand {
    Add {
        title: String,
        #[arg(long)]
        every: u64,
    },
    List,
    Done { id: String },
}

#[tokio::main]
async fn main() -> Result<()> {
    let cli = Cli::parse();
    let config = Config::load_or_default(cli.config.as_deref())?;
    let app = AssistantApp::new(config).await?;

    match cli.command.unwrap_or(Command::Chat) {
        Command::Chat => app.run_chat().await,
        Command::Ask { prompt } => app.ask_once(&prompt).await,
        Command::Task { command } => match command {
            TaskCommand::Add { title, every } => app.add_task(&title, every).await,
            TaskCommand::List => app.list_tasks().await,
            TaskCommand::Done { id } => app.complete_task(&id).await,
        },
        Command::Doctor => app.doctor().await,
        Command::Init => app.init_files().await,
    }
}
