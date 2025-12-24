# PicoScope 4000a

## Environment setup

### For Windows

This section describes a minimal, reliable setup for using **Pico Technology’s `picosdk-python-wrappers`** on **Windows**.

---

#### 0) Prerequisites

- Windows 10/11
- A PicoScope driver package (installed via Pico SDK and/or PicoScope application)
- Anaconda / Miniforge installed
- Administrator privileges (recommended for driver installation)

---

#### 1) Install PicoSDK (C libraries / drivers)

1. Download and install **PicoSDK** for Windows from the [download page](https://www.picotech.com/downloads)

    1. Select `PicoScope 4000 Series`
    2. Select `PicoScope 4824A`
    3. Download **PicoSDK** for Windows and install it

2. During installation, ensure the **driver DLLs** are installed (e.g., `ps4000a.dll`, `ps5000a.dll`, etc.)

> Notes:
> - Some models’ dependencies are also installed with **PicoScope 7**. If you encounter missing dependency errors later, installing PicoScope can help.

---

#### 2) Add PicoSDK `lib` folder to `PATH`

1. Locate the SDK `lib` directory
    
    Typical locations are:
    
    - 64-bit: `C:\Program Files\Pico Technology\SDK\lib`
    - 32-bit: `C:\Program Files (x86)\Pico Technology\SDK\lib`
    
    Inside this folder, you should see driver DLLs such as `psXXXX.dll` (example: `ps4000a.dll`).

2. Add to PATH (System-wide recommended)
    1. Open **Start** → search **“Environment Variables”** → *Edit the system environment variables*

    2. Click **Environment Variables…**

    3. Under **System variables**, select **Path** → **Edit**

    4. Click **New** and add the SDK `lib` folder path, e.g.:

        - `C:\Program Files\Pico Technology\SDK\lib`

    5. Click **OK** to apply all dialogs.

---

#### 3) Setup Conda environment

1. Create a new environement of **Python 3.13**

    ```shell
    conda create -n pico python=3.13
    ```

2. Clone [picosdk-python-wrappers](https://github.com/picotech/picosdk-python-wrappers)

3. Navigate into the repository and install it along with other dependencies

    ```shell
    cd picosdk-python-wrappers
    pip install .
    pip install -r requirements.txt
    pip install -r requirements-for-examples.txt
    ```
