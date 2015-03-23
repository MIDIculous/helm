/* Copyright 2013-2015 Matt Tytel
 *
 * twytch is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * twytch is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with twytch.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "twytch_module.h"

#include "switch.h"

namespace mopo {

  TwytchModule::TwytchModule() { }

  Processor* TwytchModule::createMonoModControl(std::string name, mopo_float start_val,
                                                bool control_rate, bool smooth_value,
                                                ControlSkewType skew) {
    ProcessorRouter* mono_owner = getMonoRouter();
    Value* val = 0;
    if (smooth_value) {
      val = new SmoothValue(start_val);
      mono_owner->addProcessor(val);
    }
    else
      val = new Value(start_val);

    controls_[name] = val;

    VariableAdd* mono_total = new VariableAdd();
    mono_total->setControlRate(control_rate);
    mono_total->plugNext(val);
    mono_owner->addProcessor(mono_total);
    mono_mod_destinations_[name] = mono_total;
    mono_modulation_readout_[name] = mono_total->output();

    if (skew == kQuadratic) {
      PolynomialScale* polynomial_scale = new PolynomialScale(2.0);
      polynomial_scale->plug(mono_total);
      mono_owner->addProcessor(polynomial_scale);
      return polynomial_scale;
    }
    if (skew == kExponential) {
      ExponentialScale* exponential_scale = new ExponentialScale(2.0);
      exponential_scale->plug(mono_total);
      mono_owner->addProcessor(exponential_scale);
      return exponential_scale;
    }
    return mono_total;
  }

  Processor* TwytchModule::createPolyModControl(std::string name, mopo_float start_val,
                                                bool control_rate, bool smooth_value,
                                                ControlSkewType skew) {
    Processor* mono_total = createMonoModControl(name, start_val, control_rate,
                                                 smooth_value, kLinear);
    ProcessorRouter* poly_owner = getPolyRouter();

    VariableAdd* poly_total = new VariableAdd();
    poly_total->setControlRate(control_rate);
    poly_owner->addProcessor(poly_total);
    poly_mod_destinations_[name] = poly_total;

    Add* modulation_total = new Add();
    modulation_total->setControlRate(control_rate);
    modulation_total->plug(mono_total, 0);
    modulation_total->plug(poly_total, 1);
    poly_owner->addProcessor(modulation_total);

    poly_owner->registerOutput(poly_total->output());
    poly_modulation_readout_[name] = poly_owner->output(poly_owner->numOutputs() - 1);

    if (skew == kQuadratic) {
      PolynomialScale* polynomial_scale = new PolynomialScale(2.0);
      polynomial_scale->plug(modulation_total);
      poly_owner->addProcessor(polynomial_scale);
      return polynomial_scale;
    }
    if (skew == kExponential) {
      ExponentialScale* exponential_scale = new ExponentialScale(2.0);
      exponential_scale->plug(modulation_total);
      poly_owner->addProcessor(exponential_scale);
      return exponential_scale;
    }
    return modulation_total;
  }

  Processor* TwytchModule::createTempoSyncSwitch(std::string name, Processor* frequency,
                                                 Processor* bps, bool poly) {
    ProcessorRouter* owner = poly ? getPolyRouter() : getMonoRouter();
    Processor* tempo = createMonoModControl(name + "_tempo", 0.0, false);
    Switch* choose_tempo = new Switch();
    choose_tempo->plug(tempo, Switch::kSource);
    for (int i = 0; i < sizeof(synced_freq_ratios) / sizeof(mopo_float); ++i) {
      Value* tempo = new Value(synced_freq_ratios[i]);
      choose_tempo->plugNext(tempo);
    }

    Multiply* tempo_frequency = new Multiply();
    tempo_frequency->plug(choose_tempo, 0);
    tempo_frequency->plug(bps, 1);
    owner->addProcessor(choose_tempo);
    owner->addProcessor(tempo_frequency);

    Switch* choose_frequency = new Switch();
    Value* sync = new Value(1);
    choose_frequency->plug(sync, Switch::kSource);
    choose_frequency->plugNext(frequency);
    choose_frequency->plugNext(tempo_frequency);
    owner->addProcessor(choose_frequency);
    controls_[name + "_sync"] = sync;
    return choose_frequency;
  }

  control_map TwytchModule::getControls() {
    control_map all_controls = controls_;
    for (TwytchModule* sub_module : sub_modules_) {
      control_map sub_controls = sub_module->getControls();
      all_controls.insert(sub_controls.begin(), sub_controls.end());
    }

    return all_controls;
  }

  Processor::Output* TwytchModule::getModulationSource(std::string name) {
    if (mod_sources_.count(name))
      return mod_sources_[name];

    for (TwytchModule* sub_module : sub_modules_) {
      Processor::Output* source = sub_module->getModulationSource(name);
      if (source)
        return source;
    }

    return 0;
  }

  Processor* TwytchModule::getModulationDestination(std::string name, bool poly) {
    if (poly)
      return getPolyModulationDestination(name);
    return getMonoModulationDestination(name);
  }

  Processor* TwytchModule::getMonoModulationDestination(std::string name) {
    if (mono_mod_destinations_.count(name))
      return mono_mod_destinations_[name];

    for (TwytchModule* sub_module : sub_modules_) {
      Processor* destination = sub_module->getMonoModulationDestination(name);
      if (destination)
        return destination;
    }

    return 0;
  }

  Processor* TwytchModule::getPolyModulationDestination(std::string name) {
    if (poly_mod_destinations_.count(name))
      return poly_mod_destinations_[name];

    for (TwytchModule* sub_module : sub_modules_) {
      Processor* destination = sub_module->getPolyModulationDestination(name);
      if (destination)
        return destination;
    }

    return 0;
  }

  output_map TwytchModule::getModulationSources() {
    output_map all_sources = mod_sources_;
    for (TwytchModule* sub_module : sub_modules_) {
      output_map sub_sources = sub_module->getModulationSources();
      all_sources.insert(sub_sources.begin(), sub_sources.end());
    }

    return all_sources;
  }

  output_map TwytchModule::getMonoModulations() {
    output_map all_readouts = mono_modulation_readout_;
    for (TwytchModule* sub_module : sub_modules_) {
      output_map sub_readouts = sub_module->getMonoModulations();
      all_readouts.insert(sub_readouts.begin(), sub_readouts.end());
    }

    return all_readouts;
  }

  output_map TwytchModule::getPolyModulations() {
    output_map all_readouts = poly_modulation_readout_;
    for (TwytchModule* sub_module : sub_modules_) {
      output_map sub_readouts = sub_module->getPolyModulations();
      all_readouts.insert(sub_readouts.begin(), sub_readouts.end());
    }

    return all_readouts;
  }

} // namespace mopo