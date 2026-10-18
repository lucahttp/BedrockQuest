use libaxml::{Document, ResourceTable};
use std::env;
use std::fs;
use std::io::Read;
use std::path::Path;
use std::process;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = env::args().collect();
    if args.len() < 3 {
        eprintln!("Usage: pvr_manifest <input_axml_or_apk> <output_axml>");
        process::exit(1);
    }

    let input_path = &args[1];
    let output_path = &args[2];

    let raw_bytes = if input_path.ends_with(".apk") {
        let file = fs::File::open(input_path)?;
        let mut zip = zip::ZipArchive::new(file)?;
        let mut manifest_entry = zip.by_name("AndroidManifest.xml")?;
        let mut buf = Vec::new();
        manifest_entry.read_to_end(&mut buf)?;
        buf
    } else {
        fs::read(input_path)?
    };

    let doc = Document::from_axml(&raw_bytes)?;
    let mut xml = doc.to_xml();

    // 1. Add uses-feature and permissions under <manifest>
    if !xml.contains("android.hardware.vr.headtracking") {
        let pvr_features = r#"
  <uses-feature android:name="android.hardware.vr.headtracking" android:required="true"/>
  <uses-permission android:name="org.khronos.openxr.permission.OPENXR"/>
  <uses-permission android:name="org.khronos.openxr.permission.OPENXR_SYSTEM"/>
"#;
        if let Some(pos) = xml.find("<uses-sdk ") {
            xml.insert_str(pos, pvr_features);
        } else if let Some(pos) = xml.find("<uses-permission ") {
            xml.insert_str(pos, pvr_features);
        }
    }

    // 2. Add OpenXR queries under <queries>
    if !xml.contains("org.khronos.openxr.runtime_broker") {
        let pvr_queries = r#"
    <provider android:authorities="org.khronos.openxr.runtime_broker;org.khronos.openxr.system_runtime_broker"/>
    <intent>
      <action android:name="org.khronos.openxr.OpenXRRuntimeService"/>
    </intent>
    <intent>
      <action android:name="org.khronos.openxr.OpenXRApiLayerService"/>
    </intent>
"#;
        if let Some(pos) = xml.find("<queries>") {
            xml.insert_str(pos + "<queries>".len(), pvr_queries);
        }
    }

    // 3. Add pvr application metadata
    if !xml.contains("pvr.app.type") {
        let pvr_app_meta = r#"
    <meta-data android:name="pvr.supportedDevices" android:value="pico_4_ultra|pico_4|pico_neo3|pico_g3"/>
    <meta-data android:name="pvr.app.type" android:value="vr"/>
    <meta-data android:name="pvr.display.orientation" android:value="180"/>
    <meta-data android:name="pvr.app.inputType" android:value="controller,hands"/>
    <meta-data android:name="handtracking" android:value="1"/>
    <meta-data android:name="pvr.app.input.hand" android:value="true"/>
    <meta-data android:name="com.picovr.handtracking" android:value="1"/>
    <meta-data android:name="pvr.sdk.version" android:value="OpenXR"/>
    <meta-data android:name="pxr.sdk.version_code" android:value="590"/>
    <meta-data android:name="com.oculus.vr.focusaware" android:value="true"/>
    <meta-data android:name="com.oculus.intent.category.VR" android:value="vr_only"/>
"#;
        if let Some(pos) = xml.find("<application ") {
            if let Some(tag_end) = xml[pos..].find('>') {
                let insert_idx = pos + tag_end + 1;
                xml.insert_str(insert_idx, pvr_app_meta);
            }
        }
    }

    // 4. Add focusaware meta-data to MainActivity
    if let Some(pos) = xml.find(r#"android:name="android.app.lib_name""#) {
        if let Some(tag_end) = xml[pos..].find("/>") {
            let insert_idx = pos + tag_end + 2;
            let focus_meta = r#"
      <meta-data android:name="com.oculus.vr.focusaware" android:value="true"/>"#;
            xml.insert_str(insert_idx, focus_meta);
        }
    }

    // 5. Add IMMERSIVE_HMD and VR categories to MainActivity intent-filter
    if !xml.contains("IMMERSIVE_HMD") {
        let pvr_category = r#"
        <category android:name="android.intent.category.LAUNCHER"/>
        <category android:name="org.khronos.openxr.intent.category.IMMERSIVE_HMD"/>
        <category android:name="com.picovr.intent.category.VR"/>
        <category android:name="com.oculus.intent.category.VR"/>"#;
        if let Some(pos) = xml.find(r#"<category android:name="android.intent.category.LAUNCHER"/>"#) {
            let insert_idx = pos + r#"<category android:name="android.intent.category.LAUNCHER"/>"#.len();
            xml.insert_str(insert_idx, pvr_category);
        } else if let Some(pos) = xml.find(r#"<category android:name="android.intent.category.INFO"/>"#) {
            let insert_idx = pos + r#"<category android:name="android.intent.category.INFO"/>"#.len();
            xml.insert_str(insert_idx, pvr_category);
        }
    }

    // Load Android public-final.xml mapping table to guarantee standard resource IDs
    let current_exe = env::current_exe().unwrap_or_default();
    let exe_dir = current_exe.parent().unwrap_or(Path::new("."));
    let candidates = [
        exe_dir.join("public-final.xml"),
        Path::new("tools/pvr_manifest/public-final.xml").to_path_buf(),
        Path::new("/Volumes/X10Pro/Android/sdk/platforms/android-34/data/res/values/public-final.xml").to_path_buf(),
        Path::new("/Volumes/X10Pro/Android/sdk/platforms/android-35/data/res/values/public-final.xml").to_path_buf(),
    ];

    let mut table_opt = None;
    for cand in &candidates {
        if cand.exists() {
            if let Ok(t) = ResourceTable::from_public_xml(cand) {
                println!("[pvr_manifest] Loaded resource ID table from {}", cand.display());
                table_opt = Some(t);
                break;
            }
        }
    }

    let new_doc = Document::from_xml(xml.as_bytes())?;
    let new_axml = if let Some(ref table) = table_opt {
        new_doc.to_axml_with_mapping(table)?
    } else {
        eprintln!("[pvr_manifest] WARNING: No public-final.xml found; encoding without mapping");
        new_doc.to_axml()?
    };

    // Self-verification
    let verify_doc = Document::from_axml(&new_axml)?;
    let verify_xml = verify_doc.to_xml();
    assert!(verify_xml.contains("IMMERSIVE_HMD"), "Missing IMMERSIVE_HMD");
    assert!(verify_xml.contains("pvr.app.type"), "Missing pvr.app.type");
    assert!(verify_xml.contains("android.hardware.vr.headtracking"), "Missing headtracking");

    if let Some(parent) = Path::new(output_path).parent() {
        fs::create_dir_all(parent)?;
    }
    fs::write(output_path, new_axml)?;
    println!("[pvr_manifest] Wrote patched VR AXML: {} bytes -> {}", raw_bytes.len(), output_path);
    Ok(())
}
