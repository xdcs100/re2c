#include <assert.h>

#include "src/codegen/bitmap.h"
#include "src/codegen/emit.h"
#include "src/codegen/go.h"
#include "src/codegen/indent.h"
#include "src/codegen/input_api.h"
#include "src/codegen/skeleton/skeleton.h"
#include "src/dfa/dfa.h"

namespace re2c
{

static std::string genGetCondition ();
static void genCondGotoSub (OutputFile & o, uint32_t ind, RegExpIndices & vCondList, uint32_t cMin, uint32_t cMax);
static void genCondTable   (OutputFile & o, uint32_t ind, const RegExpMap & specMap);
static void genCondGoto    (OutputFile & o, uint32_t ind, const RegExpMap & specMap);
static void emit_state     (OutputFile & o, uint32_t ind, const State * s, bool used_label);

std::string genGetCondition()
{
	if (bUseYYGetConditionNaked)
	{
		return mapCodeName["YYGETCONDITION"];
	}
	else
	{
		return mapCodeName["YYGETCONDITION"] + "()";
	}
}

void genGoTo(OutputFile & o, uint32_t ind, const State *from, const State *to, bool & readCh)
{
	if (DFlag)
	{
		o << from->label << " -> " << to->label << "\n";
		return;
	}

	if (readCh && from->next != to)
	{
		o << input_api.stmt_peek (ind);
		readCh = false;
	}

	o << indent(ind) << "goto " << labelPrefix << to->label << ";\n";
}

void emit_state (OutputFile & o, uint32_t ind, const State * s, bool used_label)
{
	if (!DFlag)
	{
		if (used_label)
		{
			o << labelPrefix << s->label << ":\n";
		}
		if (dFlag && (s->action.type != Action::INITIAL))
		{
			o << indent(ind) << mapCodeName["YYDEBUG"] << "(" << s->label << ", " << input_api.expr_peek () << ");\n";
		}
		if (s->isPreCtxt)
		{
			o << input_api.stmt_backupctx (ind);
		}
	}
}

void DFA::count_used_labels (std::set<uint32_t> & used, uint32_t prolog, uint32_t start, bool force_start) const
{
	// In '-f' mode, default state is always state 0
	if (fFlag)
	{
		used.insert (0);
	}
	if (force_start)
	{
		used.insert (prolog);
	}
	for (State * s = head; s; s = s->next)
	{
		s->go.used_labels (used);
	}
	for (accept_t::const_iterator i = accept_map.begin (); i != accept_map.end (); ++i)
	{
		used.insert (i->second->label);
	}
	// must go last: it needs the set of used labels
	if (used.count (head->label))
	{
		used.insert (start);
	}
}

void DFA::emit(Output & output, uint32_t& ind, const RegExpMap* specMap, const std::string& condName, bool isLastCond, bool& bPrologBrace)
{
	OutputFile & o = output.source;

	bool bProlog = (!cFlag || !bWroteCondCheck);

	// In -c mode, the prolog needs its own label separate from start_label.
	// prolog_label is before the condition branch (GenCondGoto). It is
	// equivalent to startLabelName.
	// start_label corresponds to current condition.
	// NOTE: prolog_label must be yy0 because of the !getstate:re2c handling
	// in scanner.re
	uint32_t prolog_label = next_label;
	if (bProlog && cFlag)
	{
		next_label++;
	}

	uint32_t start_label = next_label++;

	head->action.set_initial (start_label, bSaveOnHead);


	State *s;

	for (s = head; s; s = s->next)
	{
		s->label = next_label++;
	}

	std::set<uint32_t> used_labels;
	count_used_labels (used_labels, prolog_label, start_label, o.get_force_start_label ());

	// Generate prolog
	if (bProlog)
	{
		o << "\n";
		o.insert_line_info ();

		if (DFlag)
		{
			bPrologBrace = true;
			o << "digraph re2c {\n";
		}
		else if ((!fFlag && o.get_used_yyaccept ())
		||  (!fFlag && bEmitYYCh)
		||  (bFlag && !cFlag && BitMap::first)
		||  (cFlag && !bWroteCondCheck && gFlag && !specMap->empty())
		||  (fFlag && !bWroteGetState && gFlag)
		)
		{
			bPrologBrace = true;
			o << indent(ind++) << "{\n";
		}
		else if (ind == 0)
		{
			ind = 1;
		}

		if (!fFlag && !DFlag)
		{
			if (bEmitYYCh)
			{
				o << indent(ind) << mapCodeName["YYCTYPE"] << " " << mapCodeName["yych"] << ";\n";
			}
			o.insert_yyaccept_init (ind);
		}
		else
		{
			o << "\n";
		}
	}
	if (bFlag && !cFlag && BitMap::first)
	{
		BitMap::gen(o, ind, lbChar, ubChar <= 256 ? ubChar : 256);
	}
	if (bProlog)
	{
		genCondTable(o, ind, *specMap);
		o.insert_state_goto (ind);
		if (cFlag && !DFlag)
		{
			if (used_labels.count(prolog_label))
			{
				o << labelPrefix << prolog_label << ":\n";
			}
		}
		o.write_user_start_label ();
		genCondGoto(o, ind, *specMap);
	}

	if (cFlag && !condName.empty())
	{
		if (condDivider.length())
		{
			o << replaceParam(condDivider, condDividerParam, condName) << "\n";
		}

		if (DFlag)
		{
			o << condName << " -> " << head->label << "\n";
		}
		else
		{
			o << condPrefix << condName << ":\n";
		}
	}
	if (cFlag && bFlag && BitMap::first)
	{
		o << indent(ind++) << "{\n";
		BitMap::gen(o, ind, lbChar, ubChar <= 256 ? ubChar : 256);
	}

	// The start_label is not always the first to be emitted, so we may have to jump. c.f. Initial::emit()
	if (used_labels.count(head->label))
	{
		o << indent(ind) << "goto " << labelPrefix << start_label << ";\n";
	}

	// Generate code
	for (s = head; s; s = s->next)
	{
		bool readCh = false;
		emit_state (o, ind, s, used_labels.count (s->label));
		emit_action (s->action, o, ind, readCh, s, condName, used_labels);
		s->go.emit(o, ind, readCh);
	}

	if (cFlag && bFlag && BitMap::first)
	{
		o << indent(--ind) << "}\n";
	}
	// Generate epilog
	if ((!cFlag || isLastCond) && bPrologBrace)
	{
		o << indent(--ind) << "}\n";
	}
	if (flag_skeleton)
	{
		skeleton::emit_epilog (o, ind);
	}

	// Cleanup
	if (BitMap::first)
	{
		delete BitMap::first;
		BitMap::first = NULL;
	}
}

void genCondTable(OutputFile & o, uint32_t ind, const RegExpMap& specMap)
{
	if (cFlag && !bWroteCondCheck && gFlag && specMap.size())
	{
		RegExpIndices  vCondList(specMap.size());

		for(RegExpMap::const_iterator itSpec = specMap.begin(); itSpec != specMap.end(); ++itSpec)
		{
			vCondList[itSpec->second.first] = itSpec->first;
		}

		o << indent(ind++) << "static void *" << mapCodeName["yyctable"] << "[" << specMap.size() << "] = {\n";

		for(RegExpIndices::const_iterator it = vCondList.begin(); it != vCondList.end(); ++it)
		{
			o << indent(ind) << "&&" << condPrefix << *it << ",\n";
		}
		o << indent(--ind) << "};\n";
	}
}

void genCondGotoSub(OutputFile & o, uint32_t ind, RegExpIndices& vCondList, uint32_t cMin, uint32_t cMax)
{
	if (cMin == cMax)
	{
		o << indent(ind) << "goto " << condPrefix << vCondList[cMin] << ";\n";
	}
	else
	{
		uint32_t cMid = cMin + ((cMax - cMin + 1) / 2);

		o << indent(ind) << "if (" << genGetCondition() << " < " << cMid << ") {\n";
		genCondGotoSub(o, ind + 1, vCondList, cMin, cMid - 1);
		o << indent(ind) << "} else {\n";
		genCondGotoSub(o, ind + 1, vCondList, cMid, cMax);
		o << indent(ind) << "}\n";
	}
}

void genCondGoto(OutputFile & o, uint32_t ind, const RegExpMap& specMap)
{
	if (cFlag && !bWroteCondCheck && specMap.size())
	{
		if (gFlag)
		{
			o << indent(ind) << "goto *" << mapCodeName["yyctable"] << "[" << genGetCondition() << "];\n";
		}
		else
		{
			if (sFlag)
			{
				RegExpIndices  vCondList(specMap.size());
			
				for(RegExpMap::const_iterator it = specMap.begin(); it != specMap.end(); ++it)
				{
					vCondList[it->second.first] = it->first;
				}
				genCondGotoSub(o, ind, vCondList, 0, vCondList.size() - 1);
			}
			else if (DFlag)
			{
				for(RegExpMap::const_iterator it = specMap.begin(); it != specMap.end(); ++it)
				{
					o << "0 -> " << it->first << " [label=\"state=" << it->first << "\"]\n";
				}
			}
			else
			{
				o << indent(ind) << "switch (" << genGetCondition() << ") {\n";
	
				for(RegExpMap::const_iterator it = specMap.begin(); it != specMap.end(); ++it)
				{
					o << indent(ind) << "case " << condEnumPrefix << it->first << ": goto " << condPrefix << it->first << ";\n";
				}
				o << indent(ind) << "}\n";
			}
		}
		bWroteCondCheck = true;
	}
}

void genTypes(Output & output, const RegExpMap& specMap)
{
	output.types.resize (specMap.size());
	for(RegExpMap::const_iterator itSpecMap = specMap.begin(); itSpecMap != specMap.end(); ++itSpecMap)
	{
		// If an entry is < 0 then we did the 0/empty correction twice.
		assert(itSpecMap->second.first >= 0);
		output.types[itSpecMap->second.first] = itSpecMap->first;
	}
}

} // end namespace re2c
