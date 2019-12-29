extern crate wasmer_runtime;
extern crate winapi;

use wasmer_runtime::Value;
use wasmer_runtime::imports;
use wasmer_runtime::func;
use winapi::um::fileapi::ReadFile;
use winapi::um::winnt::HANDLE;
use winapi::shared::minwindef::LPVOID;

fn print_str(ctx: &mut wasmer_runtime::Ctx, ptr: u32, len: u32) {
    // Get a slice that maps to the memory currently used by the webassembly
    // instance.
    //
    // Webassembly only supports a single memory for now,
    // but in the near future, it'll support multiple.
    //
    // Therefore, we don't assume you always just want to access first
    // memory and force you to specify the first memory.
    let memory = ctx.memory(0);

    // Get a subslice that corresponds to the memory used by the string.
    let str_vec: Vec<_> = memory.view()[ptr as usize..(ptr + len) as usize].iter().map(|cell| cell.get()).collect();

    // Convert the subslice to a `&str`.
    let string = std::str::from_utf8(&str_vec).unwrap();

    // Print it!
    println!("{}", string);
}

fn emscripten_saveSetjmp(_env: i32, _label: i32, _table: i32, _size: i32)-> i32 {
    println!("setjmp called");
    return 0;
}
fn emscripten_testSetjmp(_id: i32, _table: i32, _size: i32)-> i32 {
    panic!("testSetjmp called");
}

fn emscripten_roundf(x: f32) -> f32 {
    println!("roundf called");
    return x.round();
}

fn emscripten_invoke_vi(_0: i32, _1: i32) {
    panic!("wtf");
}
fn emscripten_invoke_iii(_0: i32, _1: i32, _2: i32) -> i32 {
    panic!("what is this shit");
}
fn emscripten_setTempRet0(_: i32) {
    panic!("setTempRet0");
}
fn emscripten_getTempRet0() -> i32 {
    panic!("getTempRet0");
}


fn w_fd_read(_0: i32, _1: i32, _2: i32, _3: i32) -> i32 {
    panic!("fd_read");
}
 fn w_fd_seek(_0: i32, _1: i64, _2: i32, _3: i32) -> i32 {
    panic!("fd_seek");
}
// fn w_fd_write(_fd: i32, _iovs: VmPtr, _iovs_len: VmSize) -> VmSize {
    // panic!("fd_write");
// }


fn tryread(handle_str: &String) {
    let handle_int : usize = handle_str.parse().unwrap();
    let handle = handle_int as HANDLE;
    let mut bytes_read: u32 = 0;
    let mut buf = [0u8; 999];
    if 0 == unsafe{
        ReadFile(handle, &mut buf[0] as *mut u8 as LPVOID, 999, &mut bytes_read, std::ptr::null_mut()
    } {
        println!("failde to readed file");
        return;
    }
    println!("{} bytes read", bytes_read);
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        std::process::exit(1);
    }
    if args.len() > 2 {
        tryread(args[2]);
    }
    let binary: Vec<u8> = std::fs::read(&args[1]).unwrap();
    let mut import_object = imports! {
        "env" => {
            "saveSetjmp" => func!(emscripten_saveSetjmp),
            "testSetjmp" => func!(emscripten_testSetjmp),
            "roundf" => func!(emscripten_roundf),
            "invoke_vi" => func!(emscripten_invoke_vi),
            "invoke_iii" => func!(emscripten_invoke_iii),
            "setTempRet0" => func!(emscripten_setTempRet0),
            "getTempRet0" => func!(emscripten_getTempRet0),
        },
        "wasi_snapshot_preview1" => {
            "fd_read" => func!(w_fd_read),
            "fd_seek" => func!(w_fd_seek),
           // "fd_write" => func!(w_fd_write),
        },
    };
    import_object.allow_missing_functions = true;
    println!("a");
    let instance = wasmer_runtime::instantiate(&binary, &import_object).unwrap();
    println!("b");
    println!("{:?}",instance.call("_start", &[]).unwrap());
    println!("c");
}

fn mainstatic() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 2 {
        std::process::exit(1);
    }
    let binary: Vec<u8> = std::fs::read(&args[1]).unwrap();
    let import_object = imports! {
    "env"=>{
        "print_str" =>  func!(print_str),
        },
    };
    println!("a");
    let instance = wasmer_runtime::instantiate(&binary, &import_object).unwrap();
    println!("b");
    println!("{:?}",instance.call("jj", &[]).unwrap());
    println!("{:?}",instance.call("_start", &[]).unwrap());
    println!("{:?}",instance.call("jj", &[]).unwrap());
    println!("{:?}",instance.call("_start", &[]).unwrap());
    println!("{:?}",instance.call("jj", &[]).unwrap());
    //println!("{:?}",instance.call("add", &[Value::I32(3),Value::I32(5)]).unwrap());
    println!("c");
}