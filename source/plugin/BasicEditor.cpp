#include "BasicEditor.h"
#include "SynthFactory.h"
#include "synthLib/midiToSysex.h"
#include "jucePluginLib/version.h"
#include "ronaldo/88emu/88lib/deviceModel.h"
#include "ronaldo/88emu/88lib/romloader.h"
#include <climits>
#include <cstdlib>
#include <cmath>
#include <fstream>

namespace retromulator
{
    static constexpr const char* kSoundFilePattern = "*.sfz;*.sf2;*.wav;*.aif;*.aiff;*.flac;*.ogg;*.zbp;*.zbb";

    // The panel refreshes at this rate; updateStatus keeps its 2 Hz pace because it scans folders.
    static constexpr int kPanelTimerHz = 16;

    // matrixLib dumps the patch EPROM into "!ROM Bank 2.syx" to "!ROM Bank 9.syx".
    static constexpr const char* kMatrixRomBankPrefix = "!ROM Bank ";

    // Name printed on the panel.
    static constexpr const char* kPanelName = "MTRX-1K";

    static bool isAkaiSampler(SynthType type) { return type == SynthType::AkaiS1000; }

    static void findSysexFiles(const juce::File& folder, juce::Array<juce::File>& out, bool recursive = false)
    {
        folder.findChildFiles(out, juce::File::findFiles, recursive, "*.syx");
        folder.findChildFiles(out, juce::File::findFiles, recursive, "*.mid");
        folder.findChildFiles(out, juce::File::findFiles, recursive, "*.bin");
        folder.findChildFiles(out, juce::File::findFiles, recursive, "*.pfm");
    }

    static void findBankFiles(SynthType type, const juce::File& folder,
                              juce::Array<juce::File>& out, bool recursive = false)
    {
        if(type == SynthType::SID)
        {
            folder.findChildFiles(out, juce::File::findFiles, recursive, "*.sng");
            folder.findChildFiles(out, juce::File::findFiles, recursive, "*.ins");
        }
        else if(type == SynthType::Ayumi)
        {
            // Presets live in the Factory/User subfolders — always search into them.
            folder.findChildFiles(out, juce::File::findFiles, true, "*.ay");
        }
        else if(type == SynthType::OPL3)
        {
            folder.findChildFiles(out, juce::File::findFiles, true, "*.sbi");
        }
        else findSysexFiles(folder, out, recursive);
    }

    static void findSoundFiles(const juce::File& folder, juce::Array<juce::File>& out, bool recursive = false)
    {
        folder.findChildFiles(out, juce::File::findFiles, recursive, kSoundFilePattern);
    }

    static juce::String normalisePath(const std::string& p)
    {
        if(p.empty()) return {};
        return juce::File(p).getFullPathName();
    }

    static bool isFolderModeBasic(SynthType type, const juce::File& synthFolder)
    {
        if(type != SynthType::MicroQ && type != SynthType::XT)
            return false;
        juce::Array<juce::File> subs;
        synthFolder.findChildFiles(subs, juce::File::findDirectories, false);
        return !subs.isEmpty();
    }

    bool BasicEditor::isFolderMode(SynthType type, const juce::File& synthFolder)
    {
        return isFolderModeBasic(type, synthFolder);
    }

    BasicEditor::BasicEditor(HeadlessProcessor& p)
        : AudioProcessorEditor(p), m_proc(p), m_about(kPanelName, g_pluginVersionString), m_editPanel(p)
    {
        setLookAndFeel(&m_lookAndFeel);

        // The sheet first, so that the corner for resizing, added below, lies on top of it.
        // The window opens at the size last chosen, and keeps the proportions of the layout.
        m_canvas.onPaint = [this](juce::Graphics& g) { paintPanel(g); };
        addAndMakeVisible(m_canvas);
        m_savedScale = juce::jlimit(kMinScale, kMaxScale, HeadlessProcessor::getUiScale());
        m_scale = juce::jmax(kMinScale, juce::jmin(m_savedScale, fittingScale(rackPanel::kHeight)));
        updateConstrainer();
        setConstrainer(&m_constrainer);
        setResizable(true, true);
        setSize(juce::roundToInt(rackPanel::kWidth * m_scale), juce::roundToInt(rackPanel::kHeight * m_scale));

        setTheme(HeadlessProcessor::isWhitePanel() ? rackPanel::Theme::White : rackPanel::Theme::Black);

        // ── Prev / Next buttons ──────────────────────────────────────────────
        // Shift+click jumps to the previous/next bank folder
        m_prevBtn.onClick = [this]
        {
            if(juce::ModifierKeys::getCurrentModifiers().isShiftDown())
                navigateBankFolder(-1);
            else
                navigatePatch(-1);
        };
        m_nextBtn.onClick = [this]
        {
            if(juce::ModifierKeys::getCurrentModifiers().isShiftDown())
                navigateBankFolder(+1);
            else
                navigatePatch(+1);
        };
        m_canvas.addAndMakeVisible(m_prevBtn);
        m_canvas.addAndMakeVisible(m_nextBtn);

        // ── Program combo ────────────────────────────────────────────────────
        m_progCombo.onChange = [this]
        {
            const int sel = m_progCombo.getSelectedId() - 1;
            if(sel < 0) return;

            const auto type = m_proc.getSynthType();
            const juce::File synthFolder(HeadlessProcessor::getSynthDataFolder(type));

            if(isFolderMode(type, synthFolder) && !m_currentBankFolder.isEmpty())
            {
                juce::Array<juce::File> patches;
                juce::File(m_currentBankFolder).findChildFiles(
                    patches, juce::File::findFiles, false, "*.syx;*.mid;*.bin;*.pfm");
                patches.sort();

                if(sel < patches.size())
                {
                    m_proc.loadPresetFromFile(
                        patches[sel].getFullPathName().toStdString(),
                        patches[sel].getFileNameWithoutExtension().toStdString());
                    updateStatus();
                }
            }
            else if(sel != m_proc.getCurrentProgram())
            {
                if(isAkaiSampler(type))
                    m_proc.selectSoundPreset(sel);
                else
                    m_proc.selectProgram(sel);
                updateStatus();
            }
        };
        m_canvas.addAndMakeVisible(m_progCombo);

        // ── Bank combo ───────────────────────────────────────────────────────
        m_bankCombo.onChange = [this]
        {
            const int id = m_bankCombo.getSelectedId();
            if(id == kImportId)
            {
                m_bankCombo.setSelectedId(0, juce::dontSendNotification);
                onLoadSysex();
                return;
            }
            if(id == kExportPresetId)
            {
                m_bankCombo.setSelectedId(0, juce::dontSendNotification);
                onExportPreset();
                return;
            }
            if(id == kExportBankId)
            {
                m_bankCombo.setSelectedId(0, juce::dontSendNotification);
                onExportBank();
                return;
            }
            if(id == kConvertToVirusB)
            {
                m_bankCombo.setSelectedId(0, juce::dontSendNotification);
                onConvertVirusBank('B');
                return;
            }
            if(id == kConvertToVirusA)
            {
                m_bankCombo.setSelectedId(0, juce::dontSendNotification);
                onConvertVirusBank('A');
                return;
            }
            if(id == kMatrixLfo1Sync || id == kMatrixLfo2Sync)
            {
                m_bankCombo.setSelectedId(0, juce::dontSendNotification);
                showMatrixLfoSyncMenu(id == kMatrixLfo1Sync ? 0 : 1);
                return;
            }
            // Switching board means a full reboot: the models differ in CPU, chipset and
            // ROM set, so it is not a live parameter.
            if(id >= kEmu88BoardFirst)
            {
                const int model = id - kEmu88BoardFirst;
                if(model == m_proc.getEmu88Model())
                    return;

                SynthFactory::setEmu88Model(model);

                startCore(SynthType::Emu88);
                return;
            }

            if(id <= 0) return;

            const int idx = id - 1;
            const auto type = m_proc.getSynthType();
            const juce::File synthFolder(HeadlessProcessor::getSynthDataFolder(type));

            if(isFolderMode(type, synthFolder))
            {
                juce::Array<juce::File> subs;
                synthFolder.findChildFiles(subs, juce::File::findDirectories, false);
                subs.sort();

                if(idx < subs.size())
                {
                    m_currentBankFolder = subs[idx].getFullPathName();

                    juce::Array<juce::File> patches;
                    subs[idx].findChildFiles(patches, juce::File::findFiles, false,
                                             "*.syx;*.mid;*.bin;*.pfm");
                    patches.sort();

                    if(!patches.isEmpty())
                        m_proc.loadPresetFromFile(
                            patches[0].getFullPathName().toStdString(),
                            patches[0].getFileNameWithoutExtension().toStdString());
                    updateStatus();
                }
            }
            else
            {
                juce::Array<juce::File> files;
                if(isAkaiSampler(type))
                    findSoundFiles(synthFolder, files);
                else
                    synthFolder.findChildFiles(files, juce::File::findFiles, false,
                                               "*.syx;*.mid;*.bin;*.pfm");
                files.sort();

                if(idx < files.size())
                {
                    if(isAkaiSampler(type))
                    {
                        m_proc.loadSoundFile(files[idx].getFullPathName().toStdString());
                    }
                    else
                    {
                        m_proc.loadPresetFromFile(files[idx].getFullPathName().toStdString(),
                                                  files[idx].getFileNameWithoutExtension().toStdString());
                    }
                    updateStatus();
                }
            }
        };
        m_canvas.addAndMakeVisible(m_bankCombo);

        // ── Status label ─────────────────────────────────────────────────────
        m_statusLabel.setJustificationType(juce::Justification::centredRight);
        m_canvas.addAndMakeVisible(m_statusLabel);

        // ── Rack panel ───────────────────────────────────────────────────────
        // Audio taper: the knob's travel is the square root of the gain.
        m_volume.onValueChange = [this]
        {
            const auto v = static_cast<float>(m_volume.getValue());
            m_proc.setOutputGain(v * v);
        };
        m_canvas.addAndMakeVisible(m_volume);

        m_selectBtn.onClick = [this]
        {
            m_mode = (m_mode == PanelMode::Patch) ? PanelMode::Bank : PanelMode::Patch;
            m_entry.clear();
            refreshPanel();
        };
        m_bankLockBtn.onClick = [this]
        {
            m_bankLock = !m_bankLock;
            refreshPanel();
        };
        m_enterBtn.onClick = [this] { commitPanelEntry(); };
        m_plusBtn.onClick  = [this] { onPanelStep(+1); };
        m_minusBtn.onClick = [this] { onPanelStep(-1); };
        m_power.onClick    = [this] { onPowerSwitch(); };
        m_themeSwitch.onClick = [this]
        {
            const auto theme = m_theme == rackPanel::Theme::White ? rackPanel::Theme::Black : rackPanel::Theme::White;
            HeadlessProcessor::setWhitePanel(theme == rackPanel::Theme::White);
            setTheme(theme);
        };

        // Names for screen readers; the legends are painted on the panel, not on the keys.
        m_volume.setTitle("Volume");
        m_selectBtn.setTitle("Select");
        m_bankLockBtn.setTitle("Bank Lock");
        m_enterBtn.setTitle("Enter");
        m_minusBtn.setTitle("-");
        m_themeSwitch.setTitle("Panel");
        m_bankCombo.setTitle("Bank");
        m_progCombo.setTitle("Patch");

        for(auto* b : { &m_selectBtn, &m_bankLockBtn, &m_enterBtn, &m_plusBtn, &m_minusBtn })
            m_canvas.addAndMakeVisible(*b);

        for(int i = 0; i < 10; ++i)
        {
            auto& b = m_digitBtns[static_cast<size_t>(i)];
            b = std::make_unique<PanelButton>(juce::String(i));
            b->onClick = [this, i] { onPanelDigit(i); };
            m_canvas.addAndMakeVisible(*b);
        }

        m_canvas.addAndMakeVisible(m_display);
        m_canvas.addAndMakeVisible(m_power);
        m_canvas.addAndMakeVisible(m_themeSwitch);

        m_aboutBtn.onClick = [this]
        {
            m_about.setVisible(true);
            m_about.toFront(false);
        };
        m_canvas.addAndMakeVisible(m_aboutBtn);

        m_manualBtn.onClick = [this] { openManual(); };
        m_canvas.addAndMakeVisible(m_manualBtn);

        m_editBtn.setClickingTogglesState(true);
        m_editBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff1f5fb0));
        m_editBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        m_editBtn.onClick = [this] { setPanelOpen(m_editBtn.getToggleState(), EditPanel::Page::Patch); };
        m_canvas.addAndMakeVisible(m_editBtn);
        m_fxBtn.setClickingTogglesState(true);
        m_fxBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff1f5fb0));
        m_fxBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        m_fxBtn.setTooltip("Effects");
        m_fxBtn.onClick = [this] { setPanelOpen(m_fxBtn.getToggleState(), EditPanel::Page::Effects); };
        m_canvas.addAndMakeVisible(m_fxBtn);
        m_listBtn.setClickingTogglesState(true);
        m_listBtn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff1f5fb0));
        m_listBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        m_listBtn.setTooltip("Presets: search, favourites and categories");
        m_listBtn.onClick = [this] { setPanelOpen(m_listBtn.getToggleState(), EditPanel::Page::Presets); };
        m_canvas.addAndMakeVisible(m_listBtn);
        m_starBtn.setTitle("Favourite");
        m_starBtn.setTooltip("Favourite");
        m_starBtn.onClick = [this]
        {
            const int program = m_proc.getCurrentProgram();
            if(m_proc.getSynthType() != SynthType::Matrix || program < 0 || program >= static_cast<int>(m_proc.getProgramNames().size()))
                return;
            presets::Library::instance().setFavourite(m_proc.getMatrixBankName(), program, m_proc.getProgramNames()[static_cast<size_t>(program)],
                                                      !m_proc.isProgramFavourite(program));
            updateStatus();
        };
        m_canvas.addAndMakeVisible(m_starBtn);
        m_editPanel.onProgramChanged = [this] { updateStatus(); };
        m_editPanel.setTheme(m_theme);
        m_canvas.addChildComponent(m_editPanel);

        m_canvas.addChildComponent(m_about);
        m_canvas.addChildComponent(m_startProgress);
        resized();

        updateStatus();
        startTimerHz(kPanelTimerHz);

        if(m_proc.m_editPanelOpen)
            setPanelOpen(true, keptPage());

        // The unit powers up as a Matrix-1000. Only the first editor of an instance does
        // this, so one that was switched off stays off when its window is reopened.
        if(m_proc.consumeAutoStart() && m_proc.getSynthType() == SynthType::None && !m_proc.m_isBooting.load())
            startCore(SynthType::Matrix);
    }

    void BasicEditor::paint(juce::Graphics& g)
    {
        // seen only where rounding leaves the sheet a pixel short of the window
        g.fillAll(juce::Colour(0xff101114));
    }

    void BasicEditor::paintPanel(juce::Graphics& g)
    {
        // The background is a picture: it is drawn again when the screen has more pixels to
        // a unit of the layout than the picture has, or far fewer.
        const int need = juce::jlimit(2, 4, static_cast<int>(std::ceil(g.getInternalContext().getPhysicalPixelScaleFactor() - 0.05f)));
        if(need != m_backgroundScale)
        {
            m_backgroundScale = need;
            rebuildBackground();
        }

        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(m_background, juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(rackPanel::kWidth), static_cast<float>(rackPanel::kHeight)));

        rackPanel::drawTitle(g, m_theme, kPanelName);
        for(int i = 0; i < rackPanel::kLedCount; ++i)
            rackPanel::drawLed(g, i, m_leds[static_cast<size_t>(i)]);
    }

    int BasicEditor::logicalHeight() const
    {
        return rackPanel::kHeight + (m_editPanel.isVisible() ? EditPanel::kHeight : 0);
    }

    void BasicEditor::updateConstrainer()
    {
        const int h = logicalHeight();
        m_constrainer.setFixedAspectRatio(static_cast<double>(rackPanel::kWidth) / h);
        m_constrainer.setSizeLimits(juce::roundToInt(rackPanel::kWidth * kMinScale), juce::roundToInt(h * kMinScale),
                                    juce::roundToInt(rackPanel::kWidth * kMaxScale), juce::roundToInt(h * kMaxScale));
    }

    // The largest scale at which the layout fits the screen the window is on, with room for
    // the frame and title bar of the window around it.
    double BasicEditor::fittingScale(const int logicalH) const
    {
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const auto* display = isShowing() ? displays.getDisplayForRect(getScreenBounds()) : displays.getPrimaryDisplay();
        if(display == nullptr)
            return kMaxScale;
        constexpr int frameW = 24, frameH = 72;
        const auto area = display->userArea;
        return juce::jmin(static_cast<double>(area.getWidth() - frameW) / rackPanel::kWidth,
                          static_cast<double>(area.getHeight() - frameH) / logicalH);
    }

    void BasicEditor::rebuildBackground()
    {
        m_background = rackPanel::createBackground(m_theme, { "Patch", "Bank", "Ready", "Boot", "No\nROM", "Error" }, m_backgroundScale);
    }

    void BasicEditor::resized()
    {
        using namespace rackPanel;

        // the sheet keeps the design size and is scaled to the window
        m_scale = static_cast<double>(getWidth()) / kWidth;
        m_canvas.setTransform(juce::AffineTransform::scale(static_cast<float>(m_scale)));
        m_canvas.setBounds(0, 0, kWidth, logicalHeight());
        if(std::abs(m_scale - m_savedScale) > 0.001)
            m_scaleSaveTicks = kPanelTimerHz / 2;

        // Rack unit, positions taken from the Matrix-1000 front panel
        constexpr int keyW = 28, keyH = 20, upperRowY = 22, lowerRowY = 56;

        m_volume     .setBounds(102, 40, 34, 34);
        m_selectBtn  .setBounds(162, lowerRowY, keyW, keyH);
        m_bankLockBtn.setBounds(423, lowerRowY, keyW, keyH);
        m_display    .setBounds(484, 27, 91, 46);
        m_enterBtn   .setBounds(605, lowerRowY, keyW, keyH);
        m_plusBtn    .setBounds(652, upperRowY, keyW, keyH);
        m_minusBtn   .setBounds(652, lowerRowY, keyW, keyH);
        for(int i = 0; i < 10; ++i)
            if(auto& b = m_digitBtns[static_cast<size_t>(i)])
                b->setBounds(697 + (i % 5) * 34, i < 5 ? upperRowY : lowerRowY, keyW, keyH);
        m_power      .setBounds(974, 31, 32, 40);
        m_themeSwitch.setBounds(kThemeSwitchX, kThemeSwitchY, kThemeSwitchW, kThemeSwitchH);

        // Browser strip
        const int y = kStripControlY, h = kStripControlH;
        m_aboutBtn  .setBounds(kAboutX, kAboutY, kAboutW, kAboutH);
        m_manualBtn .setBounds(kAboutX, kManualY, kAboutW, kAboutH);
        m_about     .setBounds(0, 0, kWidth, kHeight);
        m_startProgress.setBounds(0, 0, kWidth, kHeight);
        m_editBtn   .setBounds(kStripEditX, y, 50, h);
        m_fxBtn     .setBounds(kStripEditX + 54, y, 40, h);
        m_listBtn   .setBounds(kStripEditX + kStripEditW - 42, y, 42, h);
        m_starBtn   .setBounds(kStripStarX, y, kStripStarW, h);
        m_editPanel .setBounds(0, kHeight, kWidth, EditPanel::kHeight);
        m_bankCombo .setBounds(kStripBankX, y, kStripBankW, h);
        m_prevBtn   .setBounds(kStripNavX, y, 24, h);
        m_nextBtn   .setBounds(kStripNavX + 26, y, 24, h);
        m_progCombo .setBounds(kStripPatchX, y, kStripPatchW, h);
        m_statusLabel.setBounds(kStripStarX + kStripStarW + 4, y, kWidth - (kStripStarX + kStripStarW + 4) - 8, h);
    }

    void BasicEditor::refreshBankCombo()
    {
        const auto type = m_proc.getSynthType();

        if(type == SynthType::None)
        {
            m_bankCombo.clear(juce::dontSendNotification);
            return;
        }

        // OpenWurli is a single physical model — no banks/presets to import.
        if(type == SynthType::OpenWurli)
        {
            if(m_bankCombo.getNumItems() == 0 || m_bankCombo.getText() != "by Joshua Price")
            {
                m_bankCombo.clear(juce::dontSendNotification);
                m_bankCombo.setText("by Joshua Price", juce::dontSendNotification);
            }
            return;
        }

        // The SC family has no bank files: the combo picks which board boots, and only
        // the boards whose ROM set is complete are offered.
        if(type == SynthType::Emu88)
        {
            std::vector<int> boards;
            for(uint32_t i = 0; i < emu88Lib::deviceModelCount(); ++i)
                if(emu88Lib::RomLoader::isDeviceAvailable(static_cast<emu88Lib::DeviceModel>(i)))
                    boards.push_back(static_cast<int>(i));

            if(m_bankCombo.getNumItems() != static_cast<int>(boards.size()))
            {
                m_bankCombo.clear(juce::dontSendNotification);
                for(const int b : boards)
                    m_bankCombo.addItem(
                        emu88Lib::getDeviceProfile(static_cast<emu88Lib::DeviceModel>(b)).displayName,
                        kEmu88BoardFirst + b);
            }

            const int model = m_proc.getEmu88Model();
            m_bankCombo.setSelectedId(model >= 0 ? kEmu88BoardFirst + model : 0,
                                      juce::dontSendNotification);
            return;
        }

        const juce::File folder(HeadlessProcessor::getSynthDataFolder(type));

        if(isFolderMode(type, folder))
        {
            juce::Array<juce::File> subs;
            folder.findChildFiles(subs, juce::File::findDirectories, false);
            subs.sort();

            const int subCount = subs.size();
            const int folderExtra = (type == SynthType::VirusABC) ? 5 : 3;
            bool needsRebuild = (m_bankCombo.getNumItems() != subCount + folderExtra);
            if(!needsRebuild)
                for(int i = 0; i < subCount; ++i)
                    if(m_bankCombo.getItemText(i) != subs[i].getFileName())
                        { needsRebuild = true; break; }

            if(needsRebuild)
            {
                m_bankCombo.clear(juce::dontSendNotification);
                for(int i = 0; i < subCount; ++i)
                    m_bankCombo.addItem(subs[i].getFileName(), i + 1);
                m_bankCombo.addSeparator();
                m_bankCombo.addItem("[ + Import... ]", kImportId);
                m_bankCombo.addItem("[ Export Preset... ]", kExportPresetId);
                m_bankCombo.addItem("[ Export Bank... ]",   kExportBankId);
                if(type == SynthType::VirusABC)
                {
                    m_bankCombo.addItem("[ Convert to Virus B... ]", kConvertToVirusB);
                    m_bankCombo.addItem("[ Convert to Virus A... ]", kConvertToVirusA);
                }
            }

            int selId = 0;
            for(int i = 0; i < subs.size(); ++i)
                if(subs[i].getFullPathName() == m_currentBankFolder)
                    { selId = i + 1; break; }
            m_bankCombo.setSelectedId(selId, juce::dontSendNotification);
        }
        else
        {
            const juce::String currentPath(normalisePath(m_proc.getSysexFilePath()));

            juce::Array<juce::File> files;
            if(isAkaiSampler(type))
                findSoundFiles(folder, files);
            else
                findBankFiles(type, folder, files);
            files.sort();

            const bool akai = isAkaiSampler(type);
            const bool virusABC = (type == SynthType::VirusABC);
            // Import + ExportPreset + ExportBank = 3; +ConvertB + ConvertA = 5 for Virus ABC
            // (separators don't count in getNumItems)
            const int extraItems = akai ? 1 : (virusABC ? 5 : 3) + (type == SynthType::Matrix ? 2 : 0);
            const int fileCount = files.size();
            bool needsRebuild = (m_bankCombo.getNumItems() != fileCount + extraItems);
            if(!needsRebuild)
                for(int i = 0; i < fileCount; ++i)
                    if(m_bankCombo.getItemText(i) != files[i].getFileNameWithoutExtension())
                        { needsRebuild = true; break; }

            if(needsRebuild)
            {
                m_bankCombo.clear(juce::dontSendNotification);
                for(int i = 0; i < fileCount; ++i)
                    m_bankCombo.addItem(files[i].getFileNameWithoutExtension(), i + 1);
                m_bankCombo.addSeparator();
                m_bankCombo.addItem(akai ? "[ + Load... ]" : "[ + Import... ]", kImportId);
                if(!akai)
                {
                    m_bankCombo.addItem("[ Export Preset... ]", kExportPresetId);
                    m_bankCombo.addItem("[ Export Bank... ]",   kExportBankId);
                    if(virusABC)
                    {
                        m_bankCombo.addItem("[ Convert to Virus B... ]", kConvertToVirusB);
                        m_bankCombo.addItem("[ Convert to Virus A... ]", kConvertToVirusA);
                    }
                    if(type == SynthType::Matrix)
                    {
                        m_bankCombo.addSeparator();
                        m_bankCombo.addItem("LFO 1 Sync", kMatrixLfo1Sync);
                        m_bankCombo.addItem("LFO 2 Sync", kMatrixLfo2Sync);
                    }
                }
            }
            if(type == SynthType::Matrix)
                updateMatrixLfoSyncItems();

            int selId = 0;
            for(int i = 0; i < files.size(); ++i)
                if(files[i].getFullPathName() == currentPath)
                    { selId = i + 1; break; }

            // For Akai, if the loaded file is outside the data folder, show its
            // name in the combo even though it isn't in the folder listing.
            if(selId == 0 && akai && !currentPath.isEmpty())
            {
                const juce::String extName = juce::File(currentPath).getFileNameWithoutExtension();
                constexpr int kExternalFileId = 9000;
                m_bankCombo.clear(juce::dontSendNotification);
                m_bankCombo.addItem(extName, kExternalFileId);
                m_bankCombo.addSeparator();
                for(int i = 0; i < fileCount; ++i)
                    m_bankCombo.addItem(files[i].getFileNameWithoutExtension(), i + 1);
                m_bankCombo.addSeparator();
                m_bankCombo.addItem("[ + Load... ]", kImportId);
                selId = kExternalFileId;
            }

            m_bankCombo.setSelectedId(selId, juce::dontSendNotification);
        }
    }

    void BasicEditor::updateStatus()
    {
        // The panel has one size, so a size saved with the session is not applied.
        m_proc.consumeEditorSizeDirty();

        if(m_proc.m_isBooting.load())
        {
            m_statusLabel.setText("Booting", juce::dontSendNotification);
            m_statusLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
        }
        else if(m_proc.isFirmwareMissing() || !m_proc.getPlugin().isValid())
        {
            m_statusLabel.setText("No firmware", juce::dontSendNotification);
            m_statusLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
        }
        else if(m_proc.hasDeviceError())
        {
            m_statusLabel.setText("Device error", juce::dontSendNotification);
            m_statusLabel.setColour(juce::Label::textColourId, juce::Colours::red);
        }
        else
        {
            m_statusLabel.setText("Ready", juce::dontSendNotification);
            m_statusLabel.setColour(juce::Label::textColourId, juce::Colours::limegreen);
        }
        // Nothing to report while the unit is switched off.
        m_statusLabel.setVisible(m_proc.getSynthType() != SynthType::None);

        const juce::String currentPath(normalisePath(m_proc.getSysexFilePath()));
        // the same file can come back with other patches in it: saved into, or changed on disk
        const auto generation = m_proc.getBankGeneration();
        const bool bankChanged = (currentPath != m_lastSysexPath) || generation != m_lastBankGeneration;
        m_lastSysexPath = currentPath;
        m_lastBankGeneration = generation;

        const auto type = m_proc.getSynthType();
        const juce::File synthFolder(HeadlessProcessor::getSynthDataFolder(type));

        if(isAkaiSampler(type))
        {
            m_bankCombo.setTextWhenNothingSelected("No Bank");
            m_progCombo.setTextWhenNothingSelected("No Preset");
        }
        else
        {
            m_bankCombo.setTextWhenNothingSelected({});
            m_progCombo.setTextWhenNothingSelected({});
        }

        if(isFolderMode(type, synthFolder) && bankChanged && !currentPath.isEmpty())
        {
            const juce::File parent = juce::File(currentPath).getParentDirectory();
            if(parent.getParentDirectory().getFullPathName() == synthFolder.getFullPathName())
                m_currentBankFolder = parent.getFullPathName();
        }

        const bool folderChanged = (m_currentBankFolder != m_lastBankFolder);
        m_lastBankFolder = m_currentBankFolder;

        if(isFolderMode(type, synthFolder) && !m_currentBankFolder.isEmpty())
        {
            juce::Array<juce::File> patches;
            juce::File(m_currentBankFolder).findChildFiles(
                patches, juce::File::findFiles, false, "*.syx;*.mid;*.bin;*.pfm");
            patches.sort();

            const int patchCount = patches.size();
            if(bankChanged || folderChanged || m_progCombo.getNumItems() != patchCount)
            {
                m_progCombo.clear(juce::dontSendNotification);
                for(int i = 0; i < patchCount; ++i)
                {
                    const juce::String num = juce::String(i + 1).paddedLeft('0', 3);
                    m_progCombo.addItem(num + "  " + patches[i].getFileNameWithoutExtension(), i + 1);
                }
            }

            int selId = 0;
            for(int i = 0; i < patches.size(); ++i)
                if(patches[i].getFullPathName() == currentPath)
                    { selId = i + 1; break; }
            m_progCombo.setSelectedId(0,     juce::dontSendNotification);
            m_progCombo.setSelectedId(selId, juce::dontSendNotification);
        }
        else
        {
            const int progCount = m_proc.getProgramCount();
            const int progIdx   = m_proc.getCurrentProgram();
            const auto& names   = m_proc.getProgramNames();

            // Matrix: a filter on the presets leaves out what it does not let through, but for
            // the patch that is loaded; a favourite has a star
            const bool matrix = type == SynthType::Matrix;
            const auto revision = presets::Library::instance().getRevision();
            const auto& filter = m_proc.m_presetFilter;
            const bool presetsChanged = matrix && (revision != m_presetRevision || filter.category != m_shownFilter.category
                                                   || filter.favourites != m_shownFilter.favourites || (filter.active() && progIdx != m_shownProgram));
            m_presetRevision = revision;
            m_shownFilter = filter;
            m_shownProgram = progIdx;
            const bool countWrong = !(matrix && filter.active()) && m_progCombo.getNumItems() != progCount;
            if(bankChanged || presetsChanged || countWrong)
            {
                m_progCombo.clear(juce::dontSendNotification);
                for(int i = 0; i < progCount; ++i)
                {
                    if(matrix && i != progIdx && !m_proc.isProgramShown(i))
                        continue;
                    const juce::String name = i < static_cast<int>(names.size()) ? juce::String(names[static_cast<size_t>(i)]) : juce::String();
                    m_progCombo.addItem(programItemText(i, name), i + 1);
                }
            }
            if(matrix)
            {
                const bool favourite = m_proc.isProgramFavourite(progIdx);
                m_starBtn.setButtonText(juce::String::charToString(favourite ? juce::juce_wchar(0x2605) : juce::juce_wchar(0x2606)));
                m_starBtn.setColour(juce::TextButton::textColourOffId, favourite ? juce::Colour(0xffffc23a) : juce::Colour(0xffb9bbc0));
                m_starBtn.setEnabled(progIdx >= 0 && progCount > 0);
            }
            m_starBtn.setVisible(matrix);

            // Update current item text (name may have been set after initial load via sendBankMessage).
            // Skip for Akai — program names are already set correctly by loadSoundFile.
            // Skip for SID — instrument names from the bank file are authoritative.
            if(progCount > 0 && !isAkaiSampler(type) && type != SynthType::SID)
            {
                const juce::String patch(m_proc.getPatchName());
                const juce::String bankStr = currentPath.isEmpty()
                    ? juce::String{}
                    : juce::File(currentPath).getFileNameWithoutExtension();
                const bool hasPatchName = !patch.isEmpty() && patch != bankStr;
                m_progCombo.changeItemText(progIdx + 1, programItemText(progIdx, hasPatchName ? patch : juce::String()));
            }

            m_progCombo.setSelectedId(0,           juce::dontSendNotification);
            if(progCount > 0)
                m_progCombo.setSelectedId(progIdx + 1, juce::dontSendNotification);
        }

        const bool synthChanged = (type != m_lastSynthType);
        m_lastSynthType = type;
        if(synthChanged)
            m_bankCombo.clear(juce::dontSendNotification);

        refreshBankCombo();
        refreshPanel();
        m_editPanel.setPatchTitle(m_progCombo.getText());
    }

    void BasicEditor::onLoadSysex()
    {
        const auto type = m_proc.getSynthType();
        if(type == SynthType::None) return;

        const juce::File synthFolder(HeadlessProcessor::getSynthDataFolder(type));
        synthFolder.createDirectory();

        const auto lastFolder = HeadlessProcessor::getLastLoadFolder(type);
        const auto destFolder = lastFolder.empty() ? synthFolder : juce::File(lastFolder);

        const juce::String filter =
              isAkaiSampler(type)        ? juce::String(kSoundFilePattern)
            : type == SynthType::Ayumi   ? juce::String("*.ay")
            : type == SynthType::OPL3    ? juce::String("*.sbi")
            : type == SynthType::SID     ? juce::String("*.sng;*.ins;*.sid")
                                         : juce::String("*.syx;*.mid;*.bin;*.pfm");

        const juce::String title =
              isAkaiSampler(type)        ? "Select SFZ, SF2, ZBP, ZBB, WAV, AIF, FLAC or OGG sound file"
            : type == SynthType::Ayumi   ? "Select AY preset file"
            : type == SynthType::OPL3    ? "Select OPL SBI instrument file"
                                         : "Select SysEx patch file";

        m_fileChooser = std::make_shared<juce::FileChooser>(title, destFolder, filter);

        m_fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, type](const juce::FileChooser& fc)
            {
                const auto chosen = fc.getResult();
                if(!chosen.existsAsFile()) return;
                HeadlessProcessor::setLastLoadFolder(type, chosen.getParentDirectory().getFullPathName().toStdString());

                if(isAkaiSampler(type))
                {
                    m_proc.loadSoundFile(chosen.getFullPathName().toStdString());
                    updateStatus();
                    return;
                }

                // ── Virus ABC / TI cross-detection ──────────────────────────
                if(type == SynthType::VirusABC || type == SynthType::VirusTI)
                {
                    std::ifstream f(chosen.getFullPathName().toStdString(), std::ios::binary);
                    if(f.is_open())
                    {
                        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
                                                  std::istreambuf_iterator<char>());
                        synthLib::SysexBufferList msgs;
                        const synthLib::SysexBuffer buf(raw.begin(), raw.end());
                        synthLib::MidiToSysex::extractSysexFromData(msgs, buf);

                        const auto detected = HeadlessProcessor::detectVirusType(msgs);

                        if(detected != SynthType::None && detected != type)
                        {
                            const juce::String detectedName(synthTypeName(detected));
                            const juce::String currentName(synthTypeName(type));

                            juce::NativeMessageBox::showMessageBoxAsync(
                                juce::MessageBoxIconType::WarningIcon,
                                "Incompatible Virus Patch",
                                "This file contains " + detectedName + " patches "
                                "and is not compatible with " + currentName + ".\n\n"
                                "The file has been placed in the " + detectedName + " folder.");

                            HeadlessProcessor::copySysexToFolder(
                                chosen.getFullPathName().toStdString(), detected);
                            return;
                        }
                    }
                }

                m_proc.loadPresetFromFile(chosen.getFullPathName().toStdString(),
                                          chosen.getFileNameWithoutExtension().toStdString());
                updateStatus();
            });
    }

    void BasicEditor::onExportPreset()
    {
        const auto type = m_proc.getSynthType();
        if(type == SynthType::None || isAkaiSampler(type) || m_proc.getProgramCount() == 0) return;

        const juce::String patchName(m_proc.getPatchName());
        const juce::String defaultName = patchName.isEmpty()
            ? juce::String("preset_") + juce::String(m_proc.getCurrentProgram() + 1)
            : patchName.trim().replaceCharacters("/\\:*?\"<>|", "_________");

        const juce::File destFolder(HeadlessProcessor::getSynthDataFolder(type));

        m_fileChooser = std::make_shared<juce::FileChooser>(
            "Export Preset as SysEx",
            destFolder.getChildFile(defaultName + ".syx"),
            "*.syx");

        m_fileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& fc)
            {
                const auto chosen = fc.getResult();
                if(chosen == juce::File{}) return;

                juce::String path = chosen.getFullPathName();
                if(!path.endsWithIgnoreCase(".syx"))
                    path += ".syx";

                if(!m_proc.exportCurrentPresetToFile(path.toStdString()))
                {
                    juce::NativeMessageBox::showAsync(
                        juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::WarningIcon)
                            .withTitle("Export Failed")
                            .withMessage("Could not write preset to:\n" + path)
                            .withButton("OK"),
                        nullptr);
                }
            });
    }

    void BasicEditor::onExportBank()
    {
        const auto type = m_proc.getSynthType();
        if(type == SynthType::None || isAkaiSampler(type) || m_proc.getProgramCount() == 0) return;

        const juce::String currentPath(m_proc.getSysexFilePath());
        const juce::String defaultName = currentPath.isEmpty()
            ? "bank"
            : juce::File(currentPath).getFileNameWithoutExtension();

        const juce::File destFolder(HeadlessProcessor::getSynthDataFolder(type));

        m_fileChooser = std::make_shared<juce::FileChooser>(
            "Export Bank as SysEx",
            destFolder.getChildFile(defaultName + "_named.syx"),
            "*.syx");

        m_fileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& fc)
            {
                const auto chosen = fc.getResult();
                if(chosen == juce::File{}) return;

                juce::String path = chosen.getFullPathName();
                if(!path.endsWithIgnoreCase(".syx"))
                    path += ".syx";

                if(!m_proc.exportCurrentBankToFile(path.toStdString()))
                {
                    juce::NativeMessageBox::showAsync(
                        juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::WarningIcon)
                            .withTitle("Export Failed")
                            .withMessage("Could not write bank to:\n" + path)
                            .withButton("OK"),
                        nullptr);
                }
            });
    }

    void BasicEditor::onConvertVirusBank(char targetVersion)
    {
        const auto type = m_proc.getSynthType();
        if(type != SynthType::VirusABC || m_proc.getProgramCount() == 0) return;

        const juce::String currentPath(m_proc.getSysexFilePath());
        const juce::String defaultName = currentPath.isEmpty()
            ? "bank"
            : juce::File(currentPath).getFileNameWithoutExtension();

        const juce::String suffix = juce::String("_Virus") + juce::String::charToString(targetVersion);
        const juce::File destFolder(HeadlessProcessor::getSynthDataFolder(type));

        m_fileChooser = std::make_shared<juce::FileChooser>(
            juce::String("Convert Bank to Virus ") + juce::String::charToString(targetVersion),
            destFolder.getChildFile(defaultName + suffix + ".syx"),
            "*.syx");

        const char tv = targetVersion;
        m_fileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, tv](const juce::FileChooser& fc)
            {
                const auto chosen = fc.getResult();
                if(chosen == juce::File{}) return;

                juce::String path = chosen.getFullPathName();
                if(!path.endsWithIgnoreCase(".syx"))
                    path += ".syx";

                const int result = m_proc.exportConvertedVirusBank(path.toStdString(), tv);
                if(result < 0)
                {
                    juce::NativeMessageBox::showAsync(
                        juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::WarningIcon)
                            .withTitle("Conversion Failed")
                            .withMessage("Could not convert bank to:\n" + path)
                            .withButton("OK"),
                        nullptr);
                }
                else
                {
                    juce::NativeMessageBox::showAsync(
                        juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::InfoIcon)
                            .withTitle("Conversion Complete")
                            .withMessage(juce::String(result) + " patch(es) converted to Virus "
                                         + juce::String::charToString(tv) + " format.\n\nSaved to:\n" + path)
                            .withButton("OK"),
                        nullptr);
                }
            });
    }

    void BasicEditor::navigateFiltered(const int delta)
    {
        // in the loaded bank first
        const int count = m_proc.getProgramCount();
        for(int i = m_proc.getCurrentProgram() + delta; i >= 0 && i < count; i += delta)
        {
            if(m_proc.isProgramShown(i))
            {
                m_proc.selectProgram(i);
                updateStatus();
                return;
            }
        }
        if(m_bankLock)
            return;

        // then the banks after it, round to the loaded one: the first patch the filter lets through
        const auto banks = HeadlessProcessor::getMatrixBankFiles();
        const int n = static_cast<int>(banks.size());
        const juce::String currentPath(normalisePath(m_proc.getSysexFilePath()));
        int current = 0;
        for(int b = 0; b < n; ++b)
            if(juce::String(banks[static_cast<size_t>(b)]) == currentPath)
                current = b;
        auto& library = presets::Library::instance();
        const auto& filter = m_proc.m_presetFilter;
        for(int step = 1; step <= n; ++step)
        {
            const auto& path = banks[static_cast<size_t>(((current + delta * step) % n + n) % n)];
            const auto bank = juce::File(path).getFileNameWithoutExtension().toStdString();
            const auto names = HeadlessProcessor::getMatrixBankPatchNames(path);
            const int size = static_cast<int>(names.size());
            for(int k = 0; k < size; ++k)
            {
                const int slot = delta > 0 ? k : size - 1 - k;
                const auto& name = names[static_cast<size_t>(slot)];
                if(filter.matches(library.category(bank, slot, name), library.isFavourite(bank, slot, name)))
                {
                    m_proc.loadPresetFromFile(path, bank, slot);
                    updateStatus();
                    return;
                }
            }
        }
    }

    void BasicEditor::navigatePatch(int delta)
    {
        const auto type = m_proc.getSynthType();
        if(type == SynthType::None) return;
        if(type == SynthType::Matrix && m_proc.m_presetFilter.active())
        {
            navigateFiltered(delta);
            return;
        }

        const juce::File synthFolder(HeadlessProcessor::getSynthDataFolder(type));

        if(isFolderMode(type, synthFolder))
        {
            const juce::String currentPath(normalisePath(m_proc.getSysexFilePath()));

            if(!m_currentBankFolder.isEmpty())
            {
                juce::Array<juce::File> patches;
                juce::File(m_currentBankFolder).findChildFiles(
                    patches, juce::File::findFiles, false, "*.syx;*.mid;*.bin;*.pfm");
                patches.sort();

                int cur = -1;
                for(int i = 0; i < patches.size(); ++i)
                    if(patches[i].getFullPathName() == currentPath)
                        { cur = i; break; }

                const int next = cur + delta;
                if(next >= 0 && next < patches.size())
                {
                    m_proc.loadPresetFromFile(patches[next].getFullPathName().toStdString(),
                                              patches[next].getFileNameWithoutExtension().toStdString());
                    updateStatus();
                    return;
                }

                if(m_bankLock) return;
            }

            juce::Array<juce::File> subs;
            synthFolder.findChildFiles(subs, juce::File::findDirectories, false);
            subs.sort();
            if(subs.isEmpty()) return;

            int subCur = -1;
            for(int i = 0; i < subs.size(); ++i)
                if(subs[i].getFullPathName() == m_currentBankFolder)
                    { subCur = i; break; }

            const int nextSub = (subCur + delta + subs.size()) % subs.size();
            m_currentBankFolder = subs[nextSub].getFullPathName();

            juce::Array<juce::File> patches;
            findSysexFiles(subs[nextSub], patches);
            patches.sort();

            if(!patches.isEmpty())
            {
                const int patchIdx = (delta < 0) ? patches.size() - 1 : 0;
                m_proc.loadPresetFromFile(patches[patchIdx].getFullPathName().toStdString(),
                                          patches[patchIdx].getFileNameWithoutExtension().toStdString());
            }
            updateStatus();
        }
        else
        {
            const int count = m_proc.getProgramCount();
            const int cur   = m_proc.getCurrentProgram();

            if(count > 1)
            {
                const int next = cur + delta;
                if(next >= 0 && next < count)
                {
                    if(isAkaiSampler(type))
                        m_proc.selectSoundPreset(next);
                    else
                        m_proc.selectProgram(next);
                    updateStatus();
                    return;
                }
            }

            if(m_bankLock && count > 0) return;

            const juce::File synthFolder2(HeadlessProcessor::getSynthDataFolder(type));
            juce::Array<juce::File> files;
            if(isAkaiSampler(type))
                findSoundFiles(synthFolder2, files);
            else
                findBankFiles(type, synthFolder2, files);
            files.sort();
            if(files.isEmpty()) return;

            const juce::String currentPath(normalisePath(m_proc.getSysexFilePath()));
            int fileCur = -1;
            for(int i = 0; i < files.size(); ++i)
                if(files[i].getFullPathName() == currentPath)
                    { fileCur = i; break; }

            const int next = (fileCur + delta + files.size()) % files.size();
            if(isAkaiSampler(type))
            {
                m_proc.loadSoundFile(files[next].getFullPathName().toStdString());
            }
            else
            {
                m_proc.loadPresetFromFile(files[next].getFullPathName().toStdString(),
                                          files[next].getFileNameWithoutExtension().toStdString(),
                                          delta < 0 ? INT_MAX : 0);
            }
            updateStatus();
        }
    }

    // ── Shift+< / Shift+> : jump to previous/next bank folder ─────────────
    void BasicEditor::navigateBankFolder(int delta)
    {
        const auto type = m_proc.getSynthType();
        if(type == SynthType::None) return;

        const juce::File synthFolder(HeadlessProcessor::getSynthDataFolder(type));
        if(!isFolderMode(type, synthFolder)) return;

        juce::Array<juce::File> subs;
        synthFolder.findChildFiles(subs, juce::File::findDirectories, false);
        subs.sort();
        if(subs.isEmpty()) return;

        int subCur = -1;
        for(int i = 0; i < subs.size(); ++i)
            if(subs[i].getFullPathName() == m_currentBankFolder)
                { subCur = i; break; }

        const int nextSub = (subCur + delta + subs.size()) % subs.size();
        m_currentBankFolder = subs[nextSub].getFullPathName();

        juce::Array<juce::File> patches;
        findSysexFiles(subs[nextSub], patches);
        patches.sort();

        if(!patches.isEmpty())
        {
            m_proc.loadPresetFromFile(patches[0].getFullPathName().toStdString(),
                                      patches[0].getFileNameWithoutExtension().toStdString());
        }
        updateStatus();
    }

    void BasicEditor::updateMatrixLfoSyncItems()
    {
        for(int lfo = 0; lfo < 2; ++lfo)
        {
            const int div = m_proc.getMatrixLfoSync(lfo);
            const juce::String label = "[ LFO " + juce::String(lfo + 1) + " Sync: "
                + (div == 0 ? juce::String("Off") : juce::String(HeadlessProcessor::getMatrixLfoDivisionName(div))) + " ]";
            m_bankCombo.changeItemText(lfo == 0 ? kMatrixLfo1Sync : kMatrixLfo2Sync, label);
        }
    }

    void BasicEditor::showMatrixLfoSyncMenu(const int lfo)
    {
        juce::PopupMenu menu;
        const bool tempo = m_proc.hasMatrixHostTempo();
        const int current = m_proc.getMatrixLfoSync(lfo);
        const int count = HeadlessProcessor::getMatrixLfoDivisionCount();
        for(int d = 0; d < count; ++d)
        {
            // without a tempo every division can still be chosen; it applies once one arrives
            const bool reachable = d == 0 || !tempo || m_proc.isMatrixLfoDivisionReachable(d);
            menu.addItem(kMatrixLfoDivFirst + d, HeadlessProcessor::getMatrixLfoDivisionName(d), reachable, d == current);
        }
        if(!tempo)
        {
            menu.addSeparator();
            menu.addItem(-1, "No host tempo or MIDI clock", false, false);
        }

        juce::Component::SafePointer<BasicEditor> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_bankCombo),
            [safe, lfo, count](int r)
            {
                if(!safe || r < kMatrixLfoDivFirst || r >= kMatrixLfoDivFirst + count) return;
                safe->m_proc.setMatrixLfoSync(lfo, r - kMatrixLfoDivFirst);
                safe->updateMatrixLfoSyncItems();
                safe->updateStatus();
            });
    }

    // The user manual is a PDF that the installer puts next to the standalone app. The plugin
    // sits elsewhere and nothing is written to the registry, so the places it can be in are
    // tried in turn: next to this binary, the program folder, the data folder.
    void BasicEditor::openManual()
    {
        const juce::String name("MTRX-1K User Manual.pdf");
        const juce::String folder("GPR Music Project\\MTRX-1K");

        juce::Array<juce::File> places;
        places.add(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile(name));
        for(const char* variable : {"ProgramW6432", "ProgramFiles"})
            if(const char* programs = std::getenv(variable); programs && *programs)
                places.addIfNotAlreadyThere(juce::File(juce::String(programs)).getChildFile(folder).getChildFile(name));
        places.add(juce::File(juce::String(HeadlessProcessor::getDataFolder())).getChildFile(name));

        for(const auto& file : places)
            if(file.existsAsFile())
            {
                file.startAsProcess();
                return;
            }

        juce::NativeMessageBox::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle("MTRX-1K")
                .withMessage("The user manual was not found.\n\n"
                             "The installer puts it in\n"
                             + places[1].getFullPathName() + "\n\n"
                             "It can also be kept in the data folder:\n"
                             + places.getLast().getFullPathName())
                .withButton("OK")
                .withAssociatedComponent(this),
            [](int) {});
    }

    void BasicEditor::onFirmwareMissing(SynthType type)
    {
        m_firmwareMissingLatch = true;
        m_proc.setSynthType(SynthType::None);
        updateStatus();

        const juce::File romFolder(HeadlessProcessor::getDataFolder() + "ROM/");
        romFolder.createDirectory();

        const juce::String what = type == SynthType::Matrix
            ? juce::String("Firmware not found: the 32 KB system EPROM of the Matrix-1000 as a .bin file, "
                           "and optionally its 64 KB patch EPROM.")
            : juce::String(synthTypeName(type)) + " firmware not found.";

        juce::NativeMessageBox::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("MTRX-1K")
                .withMessage(what + "\n\n"
                             "The firmware file needs to be copied to\n"
                             + romFolder.getFullPathName() + "\n\n"
                             "The target folder will be opened once you click OK. Copy the firmware "
                             "to this folder and switch the unit on with Power.")
                .withButton("OK")
                .withAssociatedComponent(this),
            [romFolder](int) { romFolder.startAsProcess(); });
    }

    void BasicEditor::startCore(const SynthType type)
    {
        m_firmwareMissingLatch = false;
        m_entry.clear();
        m_currentBankFolder.clear();

        // The completion runs on the message thread; at once when the unit is switched off.
        juce::Component::SafePointer<BasicEditor> safe(this);
        m_proc.setSynthTypeAsync(type, {}, [safe, type]
        {
            if(safe)
                safe->onCoreStarted(type);
        });
        updateStatus();
    }

    void BasicEditor::onCoreStarted(const SynthType newType)
    {
        if(newType == SynthType::None)
        {
            updateStatus();
            return;
        }

        if(m_proc.isFirmwareMissing())
        {
            onFirmwareMissing(newType);
            return;
        }

        // A state the host restored while the core was booting stays as it is.
        if(m_proc.getProgramCount() > 0)
        {
            updateStatus();
            return;
        }

        const juce::File folder(HeadlessProcessor::getSynthDataFolder(newType));
        if(isFolderMode(newType, folder))
        {
            juce::Array<juce::File> subs;
            folder.findChildFiles(subs, juce::File::findDirectories, false);
            subs.sort();
            if(!subs.isEmpty())
            {
                m_currentBankFolder = subs[0].getFullPathName();
                juce::Array<juce::File> patches;
                findSysexFiles(subs[0], patches);
                patches.sort();
                if(!patches.isEmpty())
                    m_proc.loadPresetFromFile(patches[0].getFullPathName().toStdString(),
                                              patches[0].getFileNameWithoutExtension().toStdString());
            }
        }
        else
        {
            juce::Array<juce::File> files;
            if(isAkaiSampler(newType))
                findSoundFiles(folder, files);
            else
                findBankFiles(newType, folder, files);
            files.sort();
            if(!files.isEmpty())
            {
                if(isAkaiSampler(newType))
                    m_proc.loadSoundFile(files[0].getFullPathName().toStdString());
                else
                    m_proc.loadPresetFromFile(files[0].getFullPathName().toStdString(),
                                              files[0].getFileNameWithoutExtension().toStdString(), 0);
            }
        }

        updateStatus();
    }
    // ── Rack panel ────────────────────────────────────────────────────────

    void BasicEditor::timerCallback()
    {
        // the size the window was given is kept once the drag has rested
        if(m_scaleSaveTicks > 0 && --m_scaleSaveTicks == 0)
        {
            m_savedScale = m_scale;
            HeadlessProcessor::setUiScale(m_scale);
        }

        if(m_entryTicks > 0 && --m_entryTicks == 0)
            m_entry.clear();
        if(m_flashTicks > 0)
            --m_flashTicks;

        // The first start with a firmware takes seconds: meanwhile a panel says what is
        // being done and how far it has come. It goes when the core is up.
        const int firstStart = m_proc.m_isBooting.load() ? HeadlessProcessor::getMatrixFirstStartStage() : 0;
        if(firstStart != 0)
            m_startProgress.setProgress(firstStart == 1 ? StartProgressPanel::Stage::Calibrating : StartProgressPanel::Stage::ReadingBanks,
                                        HeadlessProcessor::getMatrixFirstStartProgress());
        if(m_startProgress.isVisible() != (firstStart != 0))
        {
            m_startProgress.setVisible(firstStart != 0);
            if(firstStart != 0)
                m_startProgress.toFront(false);
        }

        if(++m_statusTicks >= kPanelTimerHz / 2)
        {
            m_statusTicks = 0;
            // another instance may have saved into the bank that is loaded here
            m_proc.refreshMatrixBankFromDisk();
            // a session restored while the window is open brings its own setting
            if(m_proc.m_editPanelOpen != m_editPanel.isVisible() || keptPage() != m_editPanel.getPage())
                setPanelOpen(m_proc.m_editPanelOpen, keptPage());
            updateStatus();
        }
        else
        {
            refreshPanel();
        }

        if(m_editPanel.isVisible())
            m_editPanel.refresh();
    }

    void BasicEditor::refreshPanel()
    {
        const auto type    = m_proc.getSynthType();
        const bool powered = type != SynthType::None;
        const bool booting = m_proc.m_isBooting.load();

        if(powered)
            m_firmwareMissingLatch = false;

        m_power.setToggleState(powered || booting, juce::dontSendNotification);
        m_themeSwitch.setToggleState(m_theme == rackPanel::Theme::White, juce::dontSendNotification);

        // Nothing can be loaded into a unit that is off or still booting.
        const bool usable = powered && !booting && type != SynthType::OpenWurli;
        for(juce::Component* c : { static_cast<juce::Component*>(&m_bankCombo), static_cast<juce::Component*>(&m_progCombo),
                                   static_cast<juce::Component*>(&m_prevBtn), static_cast<juce::Component*>(&m_nextBtn) })
            if(c->isEnabled() != usable)
                c->setEnabled(usable);

        // Follows a gain restored with the session; left alone while it is being turned.
        if(!m_volume.isMouseButtonDown())
            m_volume.setValue(std::sqrt(juce::jlimit(0.0f, 1.0f, m_proc.getOutputGain())),
                              juce::dontSendNotification);

        juce::String text;
        if(!powered && !booting)
            text = "   ";
        else if(m_flashTicks > 0)
            text = m_flash;
        else if(m_entry.isNotEmpty())
        {
            if(m_mode == PanelMode::Bank)
                text = "b" + m_entry.paddedRight('-', 2);
            else if(patchEntryLength() == 2)   // locked Matrix bank: its digit stays put
                text = juce::String(matrixBankBase() / 100) + m_entry.paddedRight('-', 2);
            else
                text = m_entry.paddedRight('-', 3);
        }
        else if(booting)
            text = "---";
        else if(m_mode == PanelMode::Bank)
        {
            // The Matrix shows its own bank digit, the other cores the position in the bank list.
            const int n = currentBankNumber();
            if(type == SynthType::Matrix)
                text = "b " + juce::String(matrixBankBase() / 100);
            else
                text = n > 0 ? "b" + juce::String(n % 100).paddedLeft('0', 2) : juce::String("b--");
        }
        else
        {
            const int n = m_progCombo.getSelectedId();
            text = n > 0 ? juce::String(patchDisplayNumber(n - 1) % 1000).paddedLeft('0', 3) : juce::String("---");
        }
        m_display.setText(text);
        m_display.setDot(powered && m_bankLock);

        const bool noRom = powered ? (m_proc.isFirmwareMissing() || !m_proc.getPlugin().isValid())
                                   : m_firmwareMissingLatch;
        const bool error = powered && !noRom && m_proc.hasDeviceError();

        const std::array<bool, rackPanel::kLedCount> leds
        {
            powered && m_mode == PanelMode::Patch,
            powered && m_mode == PanelMode::Bank,
            powered && !booting && !noRom && !error,
            booting,
            noRom,
            error
        };

        if(leds != m_leds)
        {
            m_leds = leds;
            m_canvas.repaint(0, 0, rackPanel::kWidth, rackPanel::kUnitHeight);
        }
    }

    void BasicEditor::flashPanel(const juce::String& text)
    {
        m_flash = text;
        m_flashTicks = kPanelTimerHz;
        refreshPanel();
    }

    void BasicEditor::onPanelDigit(int digit)
    {
        if(m_proc.getSynthType() == SynthType::None || m_proc.m_isBooting.load())
            return;

        m_entry += juce::String(digit);
        m_entryTicks = kPanelTimerHz * 3;
        m_flashTicks = 0;

        // The number is taken as soon as it is complete, as on the hardware.
        const bool matrix = m_proc.getSynthType() == SynthType::Matrix;
        if(m_entry.length() >= (m_mode == PanelMode::Bank ? (matrix ? 1 : 2) : patchEntryLength()))
            commitPanelEntry();
        else
            refreshPanel();
    }

    void BasicEditor::commitPanelEntry()
    {
        if(m_entry.isEmpty())
            return;

        const int number = m_entry.getIntValue();
        m_entry.clear();
        m_entryTicks = 0;

        if(m_mode == PanelMode::Bank)
            selectBankNumber(number);
        else
            selectPatchNumber(number);
    }

    void BasicEditor::onPanelStep(int delta)
    {
        if(m_proc.getSynthType() == SynthType::None || m_proc.m_isBooting.load())
            return;

        m_entry.clear();
        if(m_mode == PanelMode::Bank)
            stepBank(delta);
        else
            navigatePatch(delta);
        refreshPanel();
    }

    int BasicEditor::matrixBankBase() const
    {
        const juce::String name = juce::File(m_proc.getSysexFilePath()).getFileNameWithoutExtension();
        return name.startsWith(kMatrixRomBankPrefix) ? name.getTrailingIntValue() * 100 : 0;
    }

    int BasicEditor::matrixBankItemId(int bank) const
    {
        // 0 and 1 are the RAM bank; without its file, the first bank that is not a ROM bank
        const juce::String wanted = bank >= 2
            ? juce::String(kMatrixRomBankPrefix) + juce::String(bank)
            : juce::File(HeadlessProcessor::getMatrixRamBankPath()).getFileNameWithoutExtension();
        int fallback = 0;
        for(const int id : bankItemIds())
        {
            const juce::String text = m_bankCombo.getItemText(m_bankCombo.indexOfItemId(id));
            if(text == wanted)
                return id;
            if(bank < 2 && fallback == 0 && !text.startsWith(kMatrixRomBankPrefix))
                fallback = id;
        }
        return fallback;
    }

    juce::String BasicEditor::programItemText(const int index, const juce::String& name) const
    {
        const juce::String num = juce::String(patchDisplayNumber(index)).paddedLeft('0', 3);
        const bool favourite = m_proc.getSynthType() == SynthType::Matrix && m_proc.isProgramFavourite(index);
        return (favourite ? juce::String::charToString(juce::juce_wchar(0x2605)) + " " : juce::String()) + (name.isEmpty() ? num : num + "  " + name);
    }

    int BasicEditor::patchDisplayNumber(int index) const
    {
        if(m_proc.getSynthType() == SynthType::Matrix)
            return matrixBankBase() + index;
        return index + 1;
    }

    int BasicEditor::patchEntryLength() const
    {
        return (m_proc.getSynthType() == SynthType::Matrix && m_bankLock) ? 2 : 3;
    }

    void BasicEditor::selectPatchNumber(int number)
    {
        int index = number - 1;   // numbered as in the patch list, from 001

        if(m_proc.getSynthType() == SynthType::Matrix)
        {
            index = number;

            if(!m_bankLock)
            {
                // The hundreds digit picks the bank. A bank that is not a ROM bank counts
                // on from 000, and is kept if it is the one already loaded.
                const int bank = number / 100;
                const bool wantRom = bank >= 2;
                const bool onRomBank = juce::File(m_proc.getSysexFilePath()).getFileNameWithoutExtension()
                                           .startsWith(kMatrixRomBankPrefix);

                int target = (!wantRom && !onRomBank) ? m_bankCombo.getSelectedId() : 0;
                if(target == 0)
                    target = matrixBankItemId(bank);

                if(target == 0)
                {
                    flashPanel("Err");
                    return;
                }

                if(wantRom)
                    index = number % 100;
                if(target != m_bankCombo.getSelectedId())
                    m_bankCombo.setSelectedId(target, juce::sendNotificationSync);
            }
        }

        if(!m_progCombo.isEnabled() || index < 0 || index >= m_progCombo.getNumItems())
        {
            flashPanel("Err");
            return;
        }

        m_progCombo.setSelectedId(index + 1, juce::sendNotificationSync);
        refreshPanel();
    }

    std::vector<int> BasicEditor::bankItemIds() const
    {
        std::vector<int> ids;
        for(int i = 0; i < m_bankCombo.getNumItems(); ++i)
        {
            const int id = m_bankCombo.getItemId(i);
            const bool isFile  = id > 0 && id < 9000;
            const bool isBoard = id >= kEmu88BoardFirst && id < kEmu88BoardFirst + 100;
            if(isFile || isBoard)
                ids.push_back(id);
        }
        return ids;
    }

    int BasicEditor::currentBankNumber() const
    {
        const auto ids = bankItemIds();
        const int selected = m_bankCombo.getSelectedId();
        for(size_t i = 0; i < ids.size(); ++i)
            if(ids[i] == selected)
                return static_cast<int>(i) + 1;
        return 0;
    }

    void BasicEditor::selectBankNumber(int number)
    {
        const auto ids = bankItemIds();
        int target = 0;
        if(m_proc.getSynthType() == SynthType::Matrix)
            target = matrixBankItemId(number);
        else if(number >= 1 && number <= static_cast<int>(ids.size()))
            target = ids[static_cast<size_t>(number - 1)];

        if(!m_bankCombo.isEnabled() || target == 0)
        {
            flashPanel("Err");
            return;
        }

        m_bankCombo.setSelectedId(target, juce::sendNotificationSync);
        refreshPanel();
    }

    void BasicEditor::stepBank(int delta)
    {
        const auto ids = bankItemIds();
        if(ids.empty() || !m_bankCombo.isEnabled())
            return;

        const int count   = static_cast<int>(ids.size());
        const int current = currentBankNumber() - 1;
        const int next    = current < 0 ? 0 : (current + delta + count) % count;
        m_bankCombo.setSelectedId(ids[static_cast<size_t>(next)], juce::sendNotificationSync);
    }

    void BasicEditor::onPowerSwitch()
    {
        // The switch does nothing while the unit boots.
        if(!m_proc.m_isBooting.load())
        {
            const auto type = m_proc.getSynthType();
            if(type != SynthType::None)
            {
                m_powerOffType = type;
                startCore(SynthType::None);
            }
            else
            {
                // A session saved with another core comes back with that one.
                startCore(m_powerOffType != SynthType::None ? m_powerOffType : SynthType::Matrix);
            }
        }
        refreshPanel();
    }

    EditPanel::Page BasicEditor::keptPage() const
    {
        return m_proc.m_presetPageOpen ? EditPanel::Page::Presets : m_proc.m_fxPageOpen ? EditPanel::Page::Effects : EditPanel::Page::Patch;
    }

    void BasicEditor::setPanelOpen(const bool open, const EditPanel::Page page)
    {
        m_proc.m_editPanelOpen = open;
        if(open)
        {
            m_proc.m_fxPageOpen = page == EditPanel::Page::Effects;
            m_proc.m_presetPageOpen = page == EditPanel::Page::Presets;
        }
        m_editBtn.setToggleState(open && page == EditPanel::Page::Patch, juce::dontSendNotification);
        m_fxBtn.setToggleState(open && page == EditPanel::Page::Effects, juce::dontSendNotification);
        m_listBtn.setToggleState(open && page == EditPanel::Page::Presets, juce::dontSendNotification);
        m_editPanel.setPage(keptPage());
        const bool resize = open != m_editPanel.isVisible();
        m_editPanel.setVisible(open);
        if(open)
            m_editPanel.refresh();
        if(!resize)
            return;
        // the same scale and new proportions, smaller if the window would leave the screen
        updateConstrainer();
        const double scale = juce::jmax(kMinScale, juce::jmin(m_scale, fittingScale(logicalHeight())));
        setSize(juce::roundToInt(rackPanel::kWidth * scale), juce::roundToInt(logicalHeight() * scale));
    }

    void BasicEditor::setTheme(const rackPanel::Theme theme)
    {
        m_theme = theme;
        rebuildBackground();
        m_themeSwitch.setToggleState(theme == rackPanel::Theme::White, juce::dontSendNotification);
        m_aboutBtn.setTheme(theme);
        m_manualBtn.setTheme(theme);
        m_editPanel.setTheme(theme);
        m_canvas.repaint();
    }
}
