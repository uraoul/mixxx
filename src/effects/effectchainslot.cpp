#include "effects/effectchainslot.h"
#include "sampleutil.h"
#include "controlpotmeter.h"

EffectChainSlot::EffectChainSlot(QObject* pParent, unsigned int iChainNumber)
        : QObject(),
          m_iChainNumber(iChainNumber),
          // The control group names are 1-indexed while internally everything is 0-indexed.
          m_group(formatGroupString(iChainNumber)) {

    m_pControlNumEffects = new ControlObject(ConfigKey(m_group, "num_effects"));
    m_pControlNumEffects->set(0.0f);
    connect(m_pControlNumEffects, SIGNAL(valueChanged(double)),
            this, SLOT(slotControlNumEffects(double)));

    m_pControlChainEnabled = new ControlObject(ConfigKey(m_group, "enabled"));
    connect(m_pControlChainEnabled, SIGNAL(valueChanged(double)),
            this, SLOT(slotControlChainEnabled(double)));

    m_pControlChainMix = new ControlPotmeter(ConfigKey(m_group, "mix"), 0.0, 1.0);
    connect(m_pControlChainMix, SIGNAL(valueChanged(double)),
            this, SLOT(slotControlChainMix(double)));

    m_pControlChainParameter = new ControlPotmeter(ConfigKey(m_group, "parameter"), 0.0, 1.0);
    connect(m_pControlChainParameter, SIGNAL(valueChanged(double)),
            this, SLOT(slotControlChainParameter(double)));

    m_pControlChainNextPreset = new ControlObject(ConfigKey(m_group, "next_chain"));
    connect(m_pControlChainNextPreset, SIGNAL(valueChanged(double)),
            this, SLOT(slotControlChainNextPreset(double)));

    m_pControlChainPrevPreset = new ControlObject(ConfigKey(m_group, "prev_chain"));
    connect(m_pControlChainPrevPreset, SIGNAL(valueChanged(double)),
            this, SLOT(slotControlChainPrevPreset(double)));

    connect(&m_groupStatusMapper, SIGNAL(mapped(const QString&)),
            this, SLOT(slotGroupStatusChanged(const QString&)));
}

EffectChainSlot::~EffectChainSlot() {
    qDebug() << debugString() << "destroyed";
    delete m_pControlNumEffects;
    delete m_pControlChainEnabled;
    delete m_pControlChainMix;
    delete m_pControlChainParameter;
    delete m_pControlChainPrevPreset;
    delete m_pControlChainNextPreset;
    m_slots.clear();
    m_pEffectChain.clear();
}

QString EffectChainSlot::id() const {
    if (m_pEffectChain)
        return m_pEffectChain->id();
    return "";
}

QString EffectChainSlot::name() const {
    if (m_pEffectChain)
        return m_pEffectChain->name();
    return tr("None");
}

void EffectChainSlot::slotChainUpdated() {
    qDebug() << debugString() << "slotChainUpdated";
    if (m_pEffectChain) {
        QList<EffectPointer> effects = m_pEffectChain->getEffects();
        while (effects.size() > m_slots.size()) {
            addEffectSlot();
        }

        for (int i = 0; i < m_slots.size(); ++i) {
            EffectSlotPointer pSlot = m_slots[i];
            EffectPointer pEffect;
            if (i < effects.size()) {
                pEffect = effects[i];
            }
            if (pSlot)
                pSlot->loadEffect(pEffect);
        }

        m_pControlNumEffects->set(m_pEffectChain->numEffects());
        m_pControlChainEnabled->set(m_pEffectChain->enabled());
        m_pControlChainMix->set(m_pEffectChain->mix());
        m_pControlChainParameter->set(m_pEffectChain->parameter());

        for (QMap<QString, ControlObject*>::iterator it = m_channelEnableControls.begin();
             it != m_channelEnableControls.end(); ++it) {
            it.value()->set(m_pEffectChain->enabledForGroup(it.key()));
        }
    }
}

void EffectChainSlot::loadEffectChain(EffectChainPointer pEffectChain) {
    qDebug() << debugString() << "loadEffectChain" << (pEffectChain ? pEffectChain->id() : "(null)");
    clear();

    if (pEffectChain) {
        m_pEffectChain = pEffectChain;
        connect(m_pEffectChain.data(), SIGNAL(updated()),
                this, SLOT(slotChainUpdated()));
        slotChainUpdated();
    }

    emit(effectChainLoaded(pEffectChain));
    emit(updated());
}

EffectChainPointer EffectChainSlot::getEffectChain() const {
    return m_pEffectChain;
}

void EffectChainSlot::clear() {
    // Stop listening to signals from any loaded effect
    if (m_pEffectChain) {
        m_pEffectChain->disconnect(this);
        m_pEffectChain.clear();
        m_pEffectChain->setEnabled(false);

        foreach (EffectSlotPointer pSlot, m_slots) {
            pSlot->loadEffect(EffectPointer());
        }

    }
    m_pControlNumEffects->set(0.0f);
    m_pControlChainEnabled->set(0.0f);
    m_pControlChainMix->set(0.0f);
    m_pControlChainParameter->set(0.0f);
}

unsigned int EffectChainSlot::numSlots() const {
    qDebug() << debugString() << "numSlots";
    return m_slots.size();
}

void EffectChainSlot::addEffectSlot() {
    qDebug() << debugString() << "addEffectSlot";

    EffectSlot* pEffectSlot = new EffectSlot(this, m_iChainNumber, m_slots.size());
    // Rebroadcast effectLoaded signals
    connect(pEffectSlot, SIGNAL(effectLoaded(EffectPointer, unsigned int)),
            this, SLOT(slotEffectLoaded(EffectPointer, unsigned int)));
    m_slots.append(EffectSlotPointer(pEffectSlot));
}

void EffectChainSlot::registerChannel(const QString channelId) {
    if (m_channelEnableControls.contains(channelId)) {
        qDebug() << debugString() << "WARNING: registerChannel already has channel registered:" << channelId;
        return;
    }
    ControlObject* pEnableControl = new ControlObject(
        ConfigKey(m_group, QString("channel_%1").arg(channelId)));
    pEnableControl->set(0.0f);
    m_channelEnableControls[channelId] = pEnableControl;
    m_groupStatusMapper.setMapping(pEnableControl, channelId);
    connect(pEnableControl, SIGNAL(valueChanged(double)),
            &m_groupStatusMapper, SLOT(map()));
}

void EffectChainSlot::slotEffectLoaded(EffectPointer pEffect, unsigned int slotNumber) {
    // const int is a safe read... don't bother locking
    emit(effectLoaded(pEffect, m_iChainNumber, slotNumber));
}

EffectSlotPointer EffectChainSlot::getEffectSlot(unsigned int slotNumber) {
    qDebug() << debugString() << "getEffectSlot" << slotNumber;
    if (slotNumber >= m_slots.size()) {
        qDebug() << "WARNING: slotNumber out of range";
        return EffectSlotPointer();
    }
    return m_slots[slotNumber];
}

void EffectChainSlot::slotControlNumEffects(double v) {
    qDebug() << debugString() << "slotControlNumEffects" << v;
    qDebug() << "WARNING: Somebody has set a read-only control. Stability may be compromised.";
}

void EffectChainSlot::slotControlChainEnabled(double v) {
    qDebug() << debugString() << "slotControlChainEnabled" << v;

    bool bEnabled = v > 0.0f;
    if (m_pEffectChain) {
        m_pEffectChain->setEnabled(bEnabled);
    }
}

void EffectChainSlot::slotControlChainMix(double v) {
    qDebug() << debugString() << "slotControlChainMix" << v;

    // Clamp to [0.0, 1.0]
    if (v < 0.0f || v > 1.0f) {
        qDebug() << debugString() << "value out of limits";
        v = math_clamp(v, 0.0f, 1.0f);
        m_pControlChainMix->set(v);
    }
    if (m_pEffectChain) {
        m_pEffectChain->setMix(v);
    }
}

void EffectChainSlot::slotControlChainParameter(double v) {
    qDebug() << debugString() << "slotControlChainParameter" << v;

    // Clamp to [0.0, 1.0]
    if (v < 0.0f || v > 1.0f) {
        qDebug() << debugString() << "value out of limits";
        v = math_clamp(v, 0.0f, 1.0f);
        m_pControlChainParameter->set(v);
    }
    if (m_pEffectChain) {
        m_pEffectChain->setParameter(v);
    }
}

void EffectChainSlot::slotControlChainNextPreset(double v) {
    qDebug() << debugString() << "slotControlChainNextPreset" << v;
    // const int read is not worth locking for
    if (v > 0)
        emit(nextChain(m_iChainNumber, m_pEffectChain));
}

void EffectChainSlot::slotControlChainPrevPreset(double v) {
    qDebug() << debugString() << "slotControlChainPrevPreset" << v;
    // const int read is not worth locking for
    if (v > 0)
        emit(prevChain(m_iChainNumber, m_pEffectChain));
}

void EffectChainSlot::slotGroupStatusChanged(const QString& group) {
    if (m_pEffectChain) {
        bool bEnable = m_channelEnableControls[group]->get() > 0;
        if (bEnable) {
            m_pEffectChain->enableForGroup(group);
        } else {
            m_pEffectChain->disableForGroup(group);
        }
    }
}
