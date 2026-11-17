// Sem console na versão final (o ajudante elevado grava um log em arquivo).
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    musix_lib::run()
}
