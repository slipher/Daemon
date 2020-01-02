extern crate wasmer_runtime;
extern crate winapi;

use std::fs::File;
use std::mem::MaybeUninit;
use wasmer_runtime::Value;
use wasmer_runtime::imports;
use wasmer_runtime::func;
use winapi::um::fileapi::ReadFile;
use winapi::um::fileapi::WriteFile;
use winapi::um::winnt::HANDLE;
use winapi::shared::minwindef::LPVOID;
use winapi::shared::minwindef::LPCVOID;

use std::io::Write; // WAT

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
    eprintln!("vm: {}", string);
}

fn emscripten_saveSetjmp(_env: i32, _label: i32, _table: i32, _size: i32)-> i32 {
    eprintln!("setjmp called");
    return 0;
}
fn emscripten_testSetjmp(_id: i32, _table: i32, _size: i32)-> i32 {
    panic!("testSetjmp called");
}

fn emscripten_roundf(x: f32) -> f32 {
    eprintln!("roundf called");
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

struct ControlHeader {
  command: i32,
  pid: u32,
  message_length: u32,
  handle_count: u32,
}

const kMessage: i32 = 2;

struct InternalHeader {
    xfer_protocol_version: u32,
    descriptor_data_bytes: u32,
    pad0: u32,
    pad1: u32,
}

fn send_message(socket: HANDLE, message: &[u8]) {
    let mut bytes: u32 = 0;
    let mut header = ControlHeader {
        command: 2, // kMessage
        pid: 0,
        message_length: 16 + 5,
        handle_count: 0,
    };
    let mut header2 = InternalHeader {
        xfer_protocol_version: 0xd3c0de01,
        descriptor_data_bytes: 0,
        pad0: 123,
        pad1: 456,
    };
    let mut more = 0u8;
    if unsafe {
        0 == WriteFile(socket, &mut header as *mut ControlHeader as LPVOID, 16, &mut bytes, std::ptr::null_mut()) ||
        0 == WriteFile(socket, &mut header2 as *mut InternalHeader as LPVOID, 16, &mut bytes, std::ptr::null_mut()) ||
        0 == WriteFile(socket, &mut more as *mut u8 as LPVOID, 1, &mut bytes, std::ptr::null_mut()) ||
        0 == WriteFile(socket, &message[0] as *const u8 as LPCVOID, message.len() as u32, &mut bytes, std::ptr::null_mut())
    } {
        //handle error
    }
}


fn handle_sync_message(ctx: &mut wasmer_runtime::Ctx, data: u32, size: u32, replybuf: u32, bufsize: u32) -> u32 {
    let memory = ctx.memory(0);
    let msg: Vec<u8> = memory.view()[data as usize..(data + size) as usize].iter().map(|cell| cell.get()).collect();
    send_message(ctx.data, &msg);
    let reply = read_message(ctx.data);
    let mut lol: &[core::cell::Cell<u8>] = &memory.view()[replybuf as usize .. (replybuf as usize) + reply.len()];
    eprintln!("reply size: {}", lol.len());
    for i in 0 .. reply.len() {
        lol[i].set(reply[i]);
    }
    return reply.len() as u32;
}

fn read_message(socket: HANDLE) -> Vec<u8> {
    let mut header = MaybeUninit::<ControlHeader>::uninit();
    let mut header2 = MaybeUninit::<InternalHeader>::uninit();
    let mut more: u8 = 0;
    let mut bytes: u32 = 0;
    if 0 == unsafe { ReadFile(socket, header.as_mut_ptr() as LPVOID, 16, &mut bytes, std::ptr::null_mut()) } {
        panic!("control header read failed");
    }
    let header = unsafe { header.assume_init() };
    if header.command != kMessage {
        panic!("unhandled command {}", header.command);
    }
    if header.handle_count != 0 {
        panic!("nonzero handle count");
    }
    if header.message_length <= 17 {
        panic!("message_length too low: {}", header.message_length);
    }
    if unsafe { 0 == ReadFile(socket, header2.as_mut_ptr() as LPVOID, 16, &mut bytes, std::ptr::null_mut()) ||
                0 == ReadFile(socket, (&mut more) as *mut u8 as LPVOID, 1, &mut bytes, std::ptr::null_mut()) } {
        panic!("read failed");
    }
    if more != 0 {
        panic!("don't know how to get more");
    }
    let header2 = unsafe { header2.assume_init() };
    if header2.descriptor_data_bytes != 0 {
        panic!("descriptor_data_bytes = {}", header2.descriptor_data_bytes);
    }
    eprintln!("headers ok");
    let len = header.message_length - 17;
    let mut v = vec![0u8; len as usize];
    
    if 0 == unsafe { ReadFile(socket, (&mut v[0]) as *mut u8 as LPVOID, len, &mut bytes, std::ptr::null_mut()) } {
        panic!("read body faile");
    }
    
    eprintln!("read succeeded len {}", len);
    
    return v;
}

fn main() {
    let mut log = File::create(&"C:/unv/Unvanquished/daemon/src/engine/sandbox/log.txt").unwrap();
    eprintln!("started");
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        std::process::exit(1);
    }
    eprintln!("enough args");
    let handle_int : usize = args[2].parse().unwrap();
    let handle = handle_int as HANDLE;
    
    send_message(handle, &[3, 0, 0, 0]);
    
    eprintln!("sent abi message");
    
    let binary: Vec<u8> = std::fs::read(&args[1]).unwrap();
    
    let mut import_object = imports! {
        // why do I have to move an int lmao
        move | | { (handle_int as LPVOID, |_| {}) },
        "env" => {
            "saveSetjmp" => func!(emscripten_saveSetjmp),
            "testSetjmp" => func!(emscripten_testSetjmp),
            "roundf" => func!(emscripten_roundf),
            "invoke_vi" => func!(emscripten_invoke_vi),
            "invoke_iii" => func!(emscripten_invoke_iii),
            "setTempRet0" => func!(emscripten_setTempRet0),
            "getTempRet0" => func!(emscripten_getTempRet0),
            "WasmLog" => func!(print_str),
            "WasmSendMsg" => func!(handle_sync_message),
        },
        "wasi_snapshot_preview1" => {
            "fd_read" => func!(w_fd_read),
            "fd_seek" => func!(w_fd_seek),
           // "fd_write" => func!(w_fd_write),
        },
    };
    import_object.allow_missing_functions = true;
    let instance = wasmer_runtime::instantiate(&binary, &import_object).unwrap();
    eprintln!("instantiated");
    instance.call("_start", &[]).unwrap();
    eprintln!("static inited");
    loop {
        let v: Vec<u8> = read_message(handle);
        let bufadr: u32 = match instance.call("GetWasmbuf", &[Value::I32(v.len() as i32)]).unwrap()[0] {
            Value::I32(x) => x as u32,
            _ => panic!(""),
        };
        eprintln!("got wasmbuf");
        let mut lol: &[core::cell::Cell<u8>] = &instance.context().memory(0).view()[bufadr as usize .. (bufadr as usize) + v.len()];
        eprintln!("lol size: {}", lol.len());
        for i in 0 .. v.len() {
            lol[i].set(v[i]);
        }
        instance.call("WasmHandleSyscall", &[Value::I32(v.len() as i32)]).unwrap();
        eprintln!("handled syscall");
    }
}

