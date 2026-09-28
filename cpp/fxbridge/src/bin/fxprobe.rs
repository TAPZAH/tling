use std::path::Path;

fn main() {
    let mut args = std::env::args().skip(1);
    let model = args.next().expect("model path");
    let src = args.next().expect("src vocab");
    let trg = args.next().unwrap_or_else(|| src.clone());
    let shortlist = args.next();
    match fxtranslate::engine::Engine::load(Path::new(&model), Path::new(&src), Path::new(&trg)) {
        Ok(engine) => {
            let engine = match &shortlist {
                Some(path) => match std::fs::read(Path::new(path)) {
                    Ok(bytes) => engine.with_shortlist_bytes(&bytes),
                    Err(error) => {
                        eprintln!("shortlist read failed: {error}");
                        return;
                    }
                },
                None => engine,
            };
            println!("loaded ok; backend={}", fxtranslate::gemm::backend());
            println!("{}", engine.translate_long("Hello world"));
        }
        Err(error) => {
            eprintln!("load failed: {error}");
            std::process::exit(1);
        }
    }
}
