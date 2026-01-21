#!/usr/bin/env python3
"""
ESP32 OTA Automatic Release Script
Automates building Arduino .ino to .bin and releasing to GitHub
"""

import os
import sys
import json
import subprocess
import requests
import argparse
from pathlib import Path
from datetime import datetime


class ArduinoReleaseAutomation:
    def __init__(self, config_file="config.json"):
        """Initialize with configuration file"""
        self.load_config(config_file)
        self.validate_config()
    
    def load_config(self, config_file):
        """Load configuration from JSON file"""
        if not os.path.exists(config_file):
            print(f"❌ Config file not found: {config_file}")
            print("Please create config.json with required settings")
            sys.exit(1)
        
        with open(config_file, 'r', encoding='utf-8') as f:
            config = json.load(f)
        
        self.github_token = config.get('github_token')
        self.github_repo = config.get('github_repo')  # format: "owner/repo"
        self.arduino_cli_path = config.get('arduino_cli_path', 'arduino-cli')
        self.board_fqbn = config.get('board_fqbn', 'esp32:esp32:esp32s3')
        self.sketch_path = config.get('sketch_path')
        self.version_file = config.get('version_file', 'version.txt')
        self.auto_increment = config.get('auto_increment', True)
        self.update_ino_file = config.get('update_ino_file', True)
    
    def validate_config(self):
        """Validate required configuration"""
        if not self.github_token:
            print("❌ GitHub token is required in config.json")
            sys.exit(1)
        
        if not self.github_repo or '/' not in self.github_repo:
            print("❌ GitHub repo must be in format 'owner/repo'")
            sys.exit(1)
        
        if not self.sketch_path or not os.path.exists(self.sketch_path):
            print(f"❌ Sketch path not found: {self.sketch_path}")
            sys.exit(1)
    
    def get_current_version(self):
        """Read current version from version.txt"""
        if os.path.exists(self.version_file):
            with open(self.version_file, 'r') as f:
                return f.read().strip()
        return "1.0.0"
    
    def increment_version(self, version, increment_type='patch'):
        """Increment version number"""
        parts = version.split('.')
        major, minor, patch = int(parts[0]), int(parts[1]), int(parts[2])
        
        if increment_type == 'major':
            major += 1
            minor = 0
            patch = 0
        elif increment_type == 'minor':
            minor += 1
            patch = 0
        else:  # patch
            patch += 1
        
        return f"{major}.{minor}.{patch}"
    
    def update_version_file(self, new_version):
        """Update version.txt with new version"""
        with open(self.version_file, 'w') as f:
            f.write(new_version)
        print(f"✅ Updated {self.version_file} to version {new_version}")
    
    def update_ino_version(self, new_version):
        """Update version in .ino file"""
        if not self.update_ino_file:
            return
        
        ino_file = self.sketch_path
        if os.path.isdir(ino_file):
            # Find .ino file in directory
            ino_files = list(Path(ino_file).glob("*.ino"))
            if ino_files:
                ino_file = str(ino_files[0])
        
        if not os.path.exists(ino_file):
            print(f"⚠️ Could not find .ino file to update")
            return
        
        # Read current content
        with open(ino_file, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Update firmware version line
        import re
        updated_content = re.sub(
            r'const\s+char\*\s+currentFirmwareVersion\s*=\s*"[^"]+";',
            f'const char* currentFirmwareVersion = "{new_version}";',
            content
        )
        
        # Update firmware URL with new version tag
        owner, repo = self.github_repo.split('/')
        bin_name = Path(ino_file).stem + ".ino.bin"
        new_url = f"https://github.com/{self.github_repo}/releases/download/v{new_version}/{bin_name}"
        
        updated_content = re.sub(
            r'const\s+char\*\s+firmwareUrl\s*=\s*"[^"]+";',
            f'const char* firmwareUrl = "{new_url}";',
            updated_content
        )
        
        # Write back
        with open(ino_file, 'w', encoding='utf-8') as f:
            f.write(updated_content)
        
        print(f"✅ Updated .ino file with version {new_version}")
    
    def build_arduino_sketch(self):
        """Compile Arduino sketch to .bin file"""
        print(f"🔨 Building Arduino sketch: {self.sketch_path}")
        
        # Ensure sketch path is absolute
        sketch_path = os.path.abspath(self.sketch_path)
        
        # Build command
        cmd = [
            self.arduino_cli_path,
            'compile',
            '--fqbn', self.board_fqbn,
            '--export-binaries',
            sketch_path
        ]
        
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                check=True
            )
            print("✅ Build successful!")
            print(result.stdout)
            
            # Find the generated .bin file
            build_dir = Path(sketch_path) / 'build' / self.board_fqbn.replace(':', '.')
            bin_files = list(build_dir.glob("*.ino.bin"))
            
            if not bin_files:
                print("❌ Could not find generated .bin file")
                sys.exit(1)
            
            bin_file = bin_files[0]
            print(f"📦 Binary file: {bin_file}")
            return str(bin_file)
            
        except subprocess.CalledProcessError as e:
            print(f"❌ Build failed:")
            print(e.stderr)
            sys.exit(1)
        except FileNotFoundError:
            print(f"❌ arduino-cli not found. Please install it or check config.json")
            print("Install: https://arduino.github.io/arduino-cli/latest/installation/")
            sys.exit(1)
    
    def create_github_release(self, version, bin_file):
        """Create GitHub release and upload binary"""
        print(f"🚀 Creating GitHub release v{version}")
        
        # GitHub API endpoints
        api_base = f"https://api.github.com/repos/{self.github_repo}"
        headers = {
            'Authorization': f'token {self.github_token}',
            'Accept': 'application/vnd.github.v3+json'
        }
        
        # Create release
        release_data = {
            'tag_name': f'v{version}',
            'name': f'Release v{version}',
            'body': f'Firmware version {version}\n\nReleased: {datetime.now().strftime("%Y-%m-%d %H:%M:%S")}',
            'draft': False,
            'prerelease': False
        }
        
        response = requests.post(
            f"{api_base}/releases",
            headers=headers,
            json=release_data
        )
        
        if response.status_code != 201:
            print(f"❌ Failed to create release: {response.json()}")
            sys.exit(1)
        
        release_info = response.json()
        upload_url = release_info['upload_url'].split('{')[0]
        print(f"✅ Release created: {release_info['html_url']}")
        
        # Upload binary file
        print(f"📤 Uploading binary file...")
        
        bin_filename = Path(bin_file).name
        with open(bin_file, 'rb') as f:
            upload_headers = headers.copy()
            upload_headers['Content-Type'] = 'application/octet-stream'
            
            response = requests.post(
                f"{upload_url}?name={bin_filename}",
                headers=upload_headers,
                data=f.read()
            )
        
        if response.status_code not in [200, 201]:
            print(f"❌ Failed to upload binary: {response.json()}")
            sys.exit(1)
        
        asset_info = response.json()
        print(f"✅ Binary uploaded: {asset_info['browser_download_url']}")
        
        return release_info
    
    def commit_and_push_version(self, new_version):
        """Commit version changes to git"""
        print(f"📝 Committing version changes...")
        
        try:
            # Add version file
            subprocess.run(['git', 'add', self.version_file], check=True)
            
            # Add .ino file if updated
            if self.update_ino_file:
                subprocess.run(['git', 'add', self.sketch_path], check=True)
            
            # Commit
            subprocess.run(
                ['git', 'commit', '-m', f'Release version {new_version}'],
                check=True
            )
            
            # Push
            subprocess.run(['git', 'push'], check=True)
            
            print(f"✅ Version changes committed and pushed")
        except subprocess.CalledProcessError as e:
            print(f"⚠️ Git operation failed: {e}")
            print("You may need to commit and push manually")
    
    def run(self, version=None, increment_type='patch', skip_build=False):
        """Main execution flow"""
        print("=" * 60)
        print("ESP32 OTA Automatic Release")
        print("=" * 60)
        
        # Determine version
        current_version = self.get_current_version()
        print(f"📌 Current version: {current_version}")
        
        if version:
            new_version = version
        elif self.auto_increment:
            new_version = self.increment_version(current_version, increment_type)
        else:
            print("❌ No version specified and auto_increment is disabled")
            sys.exit(1)
        
        print(f"🎯 New version: {new_version}")
        
        # Confirm
        confirm = input(f"\n⚠️  Proceed with release v{new_version}? (yes/no): ").lower()
        if confirm not in ['yes', 'y']:
            print("❌ Release cancelled")
            sys.exit(0)
        
        # Update version in files first
        self.update_version_file(new_version)
        self.update_ino_version(new_version)
        
        # Build sketch
        if not skip_build:
            bin_file = self.build_arduino_sketch()
        else:
            # Try to find existing bin file
            build_dir = Path(self.sketch_path) / 'build' / self.board_fqbn.replace(':', '.')
            bin_files = list(build_dir.glob("*.ino.bin"))
            if not bin_files:
                print("❌ No existing .bin file found. Cannot skip build.")
                sys.exit(1)
            bin_file = str(bin_files[0])
        
        # Create GitHub release
        release_info = self.create_github_release(new_version, bin_file)
        
        # Commit and push version changes
        self.commit_and_push_version(new_version)
        
        print("\n" + "=" * 60)
        print("✨ Release completed successfully!")
        print("=" * 60)
        print(f"Version: {new_version}")
        print(f"Release URL: {release_info['html_url']}")
        print(f"Binary: {Path(bin_file).name}")
        print("=" * 60)


def main():
    parser = argparse.ArgumentParser(
        description='ESP32 OTA Automatic Release Tool'
    )
    parser.add_argument(
        '-v', '--version',
        help='Specific version to release (e.g., 1.2.3)'
    )
    parser.add_argument(
        '-i', '--increment',
        choices=['major', 'minor', 'patch'],
        default='patch',
        help='Version increment type (default: patch)'
    )
    parser.add_argument(
        '-c', '--config',
        default='config.json',
        help='Config file path (default: config.json)'
    )
    parser.add_argument(
        '--skip-build',
        action='store_true',
        help='Skip build step and use existing binary'
    )
    
    args = parser.parse_args()
    
    # Run automation
    automation = ArduinoReleaseAutomation(args.config)
    automation.run(
        version=args.version,
        increment_type=args.increment,
        skip_build=args.skip_build
    )


if __name__ == '__main__':
    main()
